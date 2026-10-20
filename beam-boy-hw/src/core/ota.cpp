#include "core/ota.h"

#include <WiFi.h>
#include <stdlib.h>
#include <string.h>

#include "core/firmware_identity.h"
#include "core/ota_signature.h"
#include "core/storage.h"

// Discovery manifest. Overridable for a hardware test server; whatever it
// serves must still be signed by a key in ota_keys.cpp.
#ifndef BEAMBOY_OTA_MANIFEST_URL
#define BEAMBOY_OTA_MANIFEST_URL "https://beamboy.ch/firmware/stable.json"
#endif

extern const uint8_t rootca_crt_bundle_start[] asm(
    "_binary_cert_x509_crt_bundle_bin_start");

namespace beamboy {
namespace {

// No bytes for this long during any transfer is a stall.
constexpr uint32_t kInactivityTimeoutMs = 15000;
// Whole manifest fetch, including TLS handshake.
constexpr uint32_t kCheckDeadlineMs = 30000;
// Whole install, from the first request to the last byte.
constexpr uint32_t kInstallDeadlineMs = 10UL * 60UL * 1000UL;

// One flash sector. poll() writes at most kChunksPerPoll of these, and stops
// early once kPollBudgetMs is spent, so the caller can redraw and check for a
// cancel between chunks.
constexpr size_t kChunkBytes = 4096;
constexpr uint8_t kChunksPerPoll = 4;
constexpr uint32_t kPollBudgetMs = 25;

uint8_t chunk_buffer[kChunkBytes];

// Reads exactly `expected` body bytes, with both an inactivity timeout and an
// overall deadline. A connection that closes early is a failure, not a hang.
bool readExactly(HTTPClient& http, uint8_t* out, size_t expected,
                 uint32_t deadline_start_ms, uint32_t deadline_ms) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) return false;
  size_t received = 0;
  uint32_t last_data_ms = millis();
  while (received < expected) {
    if (millis() - deadline_start_ms > deadline_ms) return false;
    const int available = stream->available();
    if (available > 0) {
      size_t want = expected - received;
      if (static_cast<size_t>(available) < want) {
        want = static_cast<size_t>(available);
      }
      const int read = stream->read(out + received, want);
      if (read <= 0) return false;
      received += static_cast<size_t>(read);
      last_data_ms = millis();
      continue;
    }
    if (!stream->connected()) return false;
    if (millis() - last_data_ms > kInactivityTimeoutMs) return false;
    delay(1);
  }
  return true;
}

// Downloads the envelope into a malloc'd buffer. Returns null on success, or
// the reason for failure.
const char* fetchEnvelope(char*& out, size_t& out_length) {
  out = nullptr;
  out_length = 0;
  const uint32_t start_ms = millis();

  WiFiClientSecure secure;
  secure.setCACertBundle(rootca_crt_bundle_start);
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(kInactivityTimeoutMs);
  http.setConnectTimeout(kInactivityTimeoutMs);
  // The manifest lives at a fixed URL on our own host; a redirect there is a
  // misconfiguration, not something to follow.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  if (!http.begin(secure, BEAMBOY_OTA_MANIFEST_URL)) return "bad manifest url";

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    if (code < 0) return "manifest connect/TLS failed";
    if (code == HTTP_CODE_NOT_FOUND) return "no firmware manifest published";
    return "manifest http error";
  }

  const int size = http.getSize();
  if (size <= 0 || static_cast<size_t>(size) > fw::kMaxEnvelopeBytes) {
    http.end();
    return "manifest length missing or too large";
  }

  char* body = static_cast<char*>(malloc(static_cast<size_t>(size)));
  if (body == nullptr) {
    http.end();
    return "out of memory";
  }
  const bool ok = readExactly(http, reinterpret_cast<uint8_t*>(body),
                              static_cast<size_t>(size), start_ms,
                              kCheckDeadlineMs);
  http.end();
  if (!ok) {
    free(body);
    return "manifest download stalled or truncated";
  }
  out = body;
  out_length = static_cast<size_t>(size);
  return nullptr;
}

}  // namespace

const char* Ota::currentVersion() { return fw::kVersion; }

bool Ota::rebootPending() {
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_partition_t* running = esp_ota_get_running_partition();
  return boot != nullptr && running != nullptr &&
         boot->address != running->address;
}

void Ota::releaseInstall() {
  // Aborting without esp_ota_set_boot_partition() is what keeps a partial or
  // rejected image from ever booting.
  if (handle_open_) {
    esp_ota_abort(handle_);
    handle_open_ = false;
  }
  if (sha_open_) {
    mbedtls_sha256_free(&sha_);
    sha_open_ = false;
  }
  if (http_open_) {
    http_.end();
    http_open_ = false;
  }
  client_.stop();
  target_ = nullptr;
}

void Ota::fail(const char* reason) {
  releaseInstall();
  image_valid_ = false;
  error_ = reason;
  state_ = OtaState::kFailed;
  Serial.print(F("[ota] failed: "));
  Serial.println(reason);
}

void Ota::reset() {
  releaseInstall();
  state_ = OtaState::kIdle;
  error_ = "";
  available_version_[0] = '\0';
  memset(&image_, 0, sizeof(image_));
  image_valid_ = false;
  check_.begin(0);
}

OtaState Ota::check() {
  reset();
  state_ = OtaState::kChecking;

  // A finished install that has not been rebooted into yet must be reported,
  // not silently overwritten by a second download.
  if (rebootPending()) {
    state_ = OtaState::kStaged;
    Serial.println(F("[ota] an update is installed; restart to run it"));
    return state_;
  }
  if (WiFi.status() != WL_CONNECTED) {
    fail("no network");
    return state_;
  }

  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (next == nullptr) {
    fail("no inactive update slot");
    return state_;
  }
  if (next->size != fw::kSlotSize) {
    fail("partition layout does not match this build");
    return state_;
  }

  char* text = nullptr;
  size_t length = 0;
  const char* fetch_error = fetchEnvelope(text, length);
  if (fetch_error != nullptr) {
    fail(fetch_error);
    return state_;
  }

  fw::Envelope envelope;
  fw::ManifestError parse_error = fw::parseEnvelope(text, length, envelope);
  if (parse_error != fw::ManifestError::kNone) {
    free(text);
    fail(fw::describe(parse_error));
    return state_;
  }

  // envelope.payload_b64 points into `text`, so decode before freeing it.
  const size_t capacity = envelope.payload_b64_length / 4 * 3;
  uint8_t* payload =
      capacity > 0 && capacity <= fw::kMaxPayloadBytes
          ? static_cast<uint8_t*>(malloc(capacity))
          : nullptr;
  if (payload == nullptr) {
    free(text);
    fail("manifest payload too large or out of memory");
    return state_;
  }
  size_t payload_length = 0;
  parse_error = fw::decodePayload(envelope, payload, capacity, payload_length);
  free(text);
  if (parse_error != fw::ManifestError::kNone) {
    free(payload);
    fail(fw::describe(parse_error));
    return state_;
  }

  // Authenticate before reading a single field of the payload.
  const fw::SignatureResult signature =
      fw::verifyEnvelope(envelope, payload, payload_length);
  if (signature != fw::SignatureResult::kOk) {
    free(payload);
    fail(fw::describe(signature));
    return state_;
  }

  fw::Release* release =
      static_cast<fw::Release*>(malloc(sizeof(fw::Release)));
  if (release == nullptr) {
    free(payload);
    fail("out of memory");
    return state_;
  }
  parse_error = fw::parsePayload(reinterpret_cast<const char*>(payload),
                                 payload_length, *release);
  free(payload);
  if (parse_error != fw::ManifestError::kNone) {
    free(release);
    fail(fw::describe(parse_error));
    return state_;
  }

  const ota::DeviceIdentity device = {
      fw::kBoardId,         fw::kLayoutId,          fw::kSequence,
      fw::kUpdaterVersion,  Storage::formatVersion(), fw::kScriptApiVersion,
      next->size,
  };
  const fw::BoardImage* entry = nullptr;
  const ota::ReleaseVerdict verdict =
      ota::evaluateRelease(*release, device, entry);
  strncpy(available_version_, release->version, sizeof(available_version_) - 1);
  available_version_[sizeof(available_version_) - 1] = '\0';

  switch (verdict) {
    case ota::ReleaseVerdict::kAvailable:
      image_ = *entry;
      image_valid_ = true;
      state_ = OtaState::kAvailable;
      Serial.print(F("[ota] update available: "));
      Serial.print(fw::kVersion);
      Serial.print(F(" -> "));
      Serial.println(available_version_);
      break;
    case ota::ReleaseVerdict::kUpToDate:
    case ota::ReleaseVerdict::kOlder:
      state_ = OtaState::kUpToDate;
      Serial.print(F("[ota] up to date at "));
      Serial.print(fw::kVersion);
      Serial.print(F("; published: "));
      Serial.println(available_version_);
      break;
    case ota::ReleaseVerdict::kWrongChannel:
    case ota::ReleaseVerdict::kPrerelease:
      // The stable URL serving anything but a stable release is a publishing
      // mistake, so it is reported as a failure rather than "no update".
      fail(ota::describe(verdict));
      break;
    default:
      state_ = OtaState::kIncompatible;
      error_ = ota::describe(verdict);
      Serial.print(F("[ota] release "));
      Serial.print(available_version_);
      Serial.print(F(" not installable: "));
      Serial.println(error_);
      break;
  }
  free(release);
  return state_;
}

bool Ota::beginInstall(const InstallPreconditions& preconditions) {
  if (state_ != OtaState::kAvailable || !image_valid_) {
    fail("nothing to install");
    return false;
  }
  if (preconditions.power != ota::PowerGate::kOk) {
    fail(ota::describe(preconditions.power));
    return false;
  }
  if (!preconditions.saves_clean) {
    fail("saves could not be written; not updating");
    return false;
  }
  if (rebootPending()) {
    fail("an update is already staged; restart first");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    fail("no network");
    return false;
  }

  target_ = esp_ota_get_next_update_partition(nullptr);
  if (target_ == nullptr || target_->size != fw::kSlotSize ||
      image_.size > target_->size) {
    fail("update slot unavailable");
    return false;
  }

  install_start_ms_ = millis();
  last_data_ms_ = install_start_ms_;
  if (!openImageStream()) return false;

  // Sequential mode erases one sector ahead of each write instead of the
  // whole slot up front, which would stall the frame loop for seconds.
  if (esp_ota_begin(target_, OTA_WITH_SEQUENTIAL_WRITES, &handle_) != ESP_OK) {
    fail("could not open update slot");
    return false;
  }
  handle_open_ = true;

  mbedtls_sha256_init(&sha_);
  sha_open_ = true;
  if (mbedtls_sha256_starts_ret(&sha_, 0) != 0) {
    fail("hash init failed");
    return false;
  }

  check_.begin(image_.size);
  state_ = OtaState::kDownloading;
  Serial.print(F("[ota] installing "));
  Serial.print(available_version_);
  Serial.print(F(" ("));
  Serial.print(image_.size);
  Serial.println(F(" bytes)"));
  return true;
}

bool Ota::openImageStream() {
  client_.setCACertBundle(rootca_crt_bundle_start);
  const char* header_keys[] = {"Location"};

  // Redirects are followed by hand so every hop is checked against the
  // https-only host allowlist, and the hop count is bounded.
  String url = image_.url;
  for (uint8_t hop = 0;; hop++) {
    if (!ota::isAllowedUrl(url.c_str())) {
      fail("image URL not allowed");
      return false;
    }
    if (millis() - install_start_ms_ > kCheckDeadlineMs) {
      fail("image request timed out");
      return false;
    }

    http_.setReuse(false);
    http_.setTimeout(kInactivityTimeoutMs);
    http_.setConnectTimeout(kInactivityTimeoutMs);
    http_.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http_.begin(client_, url)) {
      fail("bad image url");
      return false;
    }
    http_open_ = true;
    http_.collectHeaders(header_keys, 1);

    const int code = http_.GET();
    if (code == HTTP_CODE_OK) break;

    if (ota::isRedirectStatus(code) && hop < ota::kMaxRedirects) {
      url = http_.header("Location");
      http_.end();
      http_open_ = false;
      client_.stop();
      continue;
    }

    if (code < 0) {
      fail("image connect/TLS failed");
    } else if (ota::isRedirectStatus(code)) {
      fail("too many redirects");
    } else {
      fail("image http error");
    }
    return false;
  }

  // Content-Length must be present and equal the signed size. Chunked or
  // mismatched bodies are refused before a single byte reaches flash.
  const int size = http_.getSize();
  if (size < 0 || static_cast<uint32_t>(size) != image_.size) {
    fail("image length does not match manifest");
    return false;
  }
  return true;
}

void Ota::pumpDownload() {
  const uint32_t poll_start_ms = millis();
  if (poll_start_ms - install_start_ms_ > kInstallDeadlineMs) {
    fail("update took too long");
    return;
  }

  WiFiClient* stream = http_.getStreamPtr();
  if (stream == nullptr) {
    fail("image stream lost");
    return;
  }

  for (uint8_t chunk = 0; chunk < kChunksPerPoll; chunk++) {
    if (check_.received() >= check_.expected()) break;
    if (millis() - poll_start_ms > kPollBudgetMs) break;

    const int available = stream->available();
    if (available <= 0) {
      if (!stream->connected()) {
        fail(ota::describe(ota::ImageError::kTruncated));
      } else if (millis() - last_data_ms_ > kInactivityTimeoutMs) {
        fail("image download stalled");
      }
      return;
    }

    // Deliberately not capped at the remaining size: a server sending more
    // than was signed is caught by ImageCheck instead of silently ignored.
    size_t want = kChunkBytes;
    if (static_cast<size_t>(available) < want) {
      want = static_cast<size_t>(available);
    }
    const int read = stream->read(chunk_buffer, want);
    if (read <= 0) {
      fail("image read failed");
      return;
    }

    const ota::ImageError image_error =
        check_.accept(chunk_buffer, static_cast<size_t>(read));
    if (image_error != ota::ImageError::kNone) {
      fail(ota::describe(image_error));
      return;
    }
    if (mbedtls_sha256_update_ret(&sha_, chunk_buffer,
                                  static_cast<size_t>(read)) != 0) {
      fail("hash update failed");
      return;
    }
    if (esp_ota_write(handle_, chunk_buffer, static_cast<size_t>(read)) !=
        ESP_OK) {
      fail("flash write failed");
      return;
    }
    last_data_ms_ = millis();
  }

  if (check_.received() == check_.expected()) state_ = OtaState::kVerifying;
}

void Ota::finishInstall() {
  WiFiClient* stream = http_.getStreamPtr();
  if (stream != nullptr && stream->available() > 0) {
    fail(ota::describe(ota::ImageError::kTooLong));
    return;
  }

  uint8_t digest[fw::kSha256Bytes];
  if (mbedtls_sha256_finish_ret(&sha_, digest) != 0) {
    fail("hash finish failed");
    return;
  }
  const ota::ImageError image_error = check_.finish(digest, image_.sha256);
  if (image_error != ota::ImageError::kNone) {
    fail(ota::describe(image_error));
    return;
  }

  http_.end();
  http_open_ = false;
  client_.stop();

  // esp_ota_end() also validates the image structure and its appended
  // checksum. The handle is invalid afterwards whatever the result.
  const esp_err_t end_result = esp_ota_end(handle_);
  handle_open_ = false;
  if (end_result != ESP_OK) {
    fail(end_result == ESP_ERR_OTA_VALIDATE_FAILED
             ? "image failed ESP-IDF validation"
             : "could not finalise update slot");
    return;
  }

  // The only place the boot target changes, after every check has passed.
  if (esp_ota_set_boot_partition(target_) != ESP_OK) {
    fail("could not select the new image");
    return;
  }

  releaseInstall();
  image_valid_ = false;
  state_ = OtaState::kSuccess;
  Serial.print(F("[ota] installed "));
  Serial.print(available_version_);
  Serial.println(F("; it runs after the next restart"));
}

OtaState Ota::poll() {
  if (state_ == OtaState::kDownloading) {
    pumpDownload();
  } else if (state_ == OtaState::kVerifying) {
    finishInstall();
  }
  return state_;
}

void Ota::cancel() {
  if (!busy()) return;
  fail("cancelled; nothing was changed");
}

}  // namespace beamboy
