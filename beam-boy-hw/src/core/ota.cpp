#include "ota.h"

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>
#else
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif

namespace beamboy {
namespace {

// Where the device looks for updates. A tiny JSON manifest rather than probing
// for a file, so the server can point at any image path and add release notes
// later without a firmware change.
constexpr char kManifestUrl[] =
    "https://beamboy.example/firmware/manifest.json";

// Build version. Bumped per release; the manifest is compared against this
// string, so any difference means "install", including a downgrade. That is
// intentional -- rolling users back off a bad release must be possible.
constexpr char kVersion[] = "0.4.0";

// TLS on the ESP8266 is the expensive part of this whole feature: BearSSL needs
// ~16-22 KB of heap for a handshake, against the ~46 KB the device has free.
// It fits only because OTA runs from the launcher with no game loaded.
//
// A smaller receive buffer is the difference between fitting and not. 1 KB is
// below the 16 KB maximum TLS record size, which works only because servers
// negotiate the smaller buffer via max_fragment_length. If a server refuses
// that extension the handshake fails -- hence the explicit error message rather
// than a bare "connection failed".
constexpr uint16_t kTlsRxBuffer = 1024;
constexpr uint16_t kTlsTxBuffer = 512;

}  // namespace

const char* Ota::currentVersion() { return kVersion; }

void Ota::fail(const char* reason) {
  error_ = reason;
  state_ = OtaState::kFailed;
  // Nothing is installable after a failure.
  image_url_[0] = '\0';
  Serial.print(F("[ota] failed: "));
  Serial.println(reason);
}

bool Ota::checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    fail("no network");
    return false;
  }

  state_ = OtaState::kChecking;
  progress_ = 0.0f;
  available_[0] = '\0';
  image_url_[0] = '\0';

  WiFiClientSecure client;
  client.setInsecure();  // See the note at the end of this file.
#if defined(ARDUINO_ARCH_ESP8266)
  client.setBufferSizes(kTlsRxBuffer, kTlsTxBuffer);
#endif

  HTTPClient http;
  if (!http.begin(client, kManifestUrl)) {
    fail("bad manifest url");
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    fail(code < 0 ? "tls/connect failed" : "manifest http error");
    return false;
  }

  const String body = http.getString();
  http.end();

  // Hand-parsed rather than pulling in a JSON library for two fields. The
  // manifest is ours, so its shape is guaranteed:
  //   {"version":"0.4.1","url":"https://.../beamboy-0.4.1.bin"}
  const int vstart = body.indexOf("\"version\"");
  const int ustart = body.indexOf("\"url\"");
  if (vstart < 0 || ustart < 0) {
    fail("malformed manifest");
    return false;
  }

  const int vq1 = body.indexOf('"', body.indexOf(':', vstart));
  const int vq2 = body.indexOf('"', vq1 + 1);
  const int uq1 = body.indexOf('"', body.indexOf(':', ustart));
  const int uq2 = body.indexOf('"', uq1 + 1);
  if (vq1 < 0 || vq2 < 0 || uq1 < 0 || uq2 < 0) {
    fail("malformed manifest");
    return false;
  }

  const String version = body.substring(vq1 + 1, vq2);
  const String url = body.substring(uq1 + 1, uq2);

  if (version.length() == 0 || version.length() >= sizeof(available_) ||
      url.length() == 0 || url.length() >= sizeof(image_url_)) {
    fail("manifest fields too long");
    return false;
  }

  strncpy(available_, version.c_str(), sizeof(available_) - 1);
  available_[sizeof(available_) - 1] = '\0';
  strncpy(image_url_, url.c_str(), sizeof(image_url_) - 1);
  image_url_[sizeof(image_url_) - 1] = '\0';

  if (version == kVersion) {
    // Clear the URL: nothing should be installable after a check that found
    // nothing to install. install() also refuses unless state is kAvailable,
    // but this object is a member of a never-destroyed static scene, so a
    // stale URL would otherwise survive across scene entries.
    image_url_[0] = '\0';
    state_ = OtaState::kUpToDate;
    Serial.print(F("[ota] already up to date at "));
    Serial.println(kVersion);
    return false;
  }

  state_ = OtaState::kAvailable;
  Serial.print(F("[ota] update available: "));
  Serial.print(kVersion);
  Serial.print(F(" -> "));
  Serial.println(available_);
  return true;
}

bool Ota::install() {
  // Refuse unless a check just found something. Guarding on state as well as on
  // the URL means no future call path can reach a reflash of the running image.
  if (state_ != OtaState::kAvailable || image_url_[0] == '\0') {
    fail("nothing to install");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    fail("no network");
    return false;
  }

  state_ = OtaState::kDownloading;
  progress_ = 0.0f;

  WiFiClientSecure client;
  client.setInsecure();
#if defined(ARDUINO_ARCH_ESP8266)
  client.setBufferSizes(kTlsRxBuffer, kTlsTxBuffer);
  ESP8266HTTPUpdate updater;
  updater.rebootOnUpdate(false);
#else
  HTTPUpdate updater;
  updater.rebootOnUpdate(false);
#endif

  // The image is written to the inactive slot. Nothing about the running
  // firmware is touched until this returns successfully.
  const auto result = updater.update(client, image_url_);

  switch (result) {
    case HTTP_UPDATE_OK:
      state_ = OtaState::kSuccess;
      progress_ = 1.0f;
      image_url_[0] = '\0';
      Serial.println(F("[ota] installed; reboot to run the new firmware"));
      return true;

    case HTTP_UPDATE_NO_UPDATES:
      state_ = OtaState::kUpToDate;
      return false;

    case HTTP_UPDATE_FAILED:
    default:
      Serial.print(F("[ota] updater said: "));
      Serial.println(updater.getLastErrorString());
      fail("download or verify failed");
      return false;
  }
}

// --- On setInsecure() --------------------------------------------------------
//
// setInsecure() skips certificate validation: the connection is encrypted but
// the server is not authenticated, so an attacker who can redirect DNS could
// serve their own firmware. That is not acceptable for a shipping product, and
// this is marked as the one thing to fix before Phase 9.
//
// It is left here deliberately rather than pinning a certificate, because
// certificate pinning is the *wrong* fix for this device: certificates expire,
// and a console that has been in a drawer for two years would be unable to
// update itself precisely when it most needs to -- a permanent, unfixable
// brick of the update path.
//
// The right answer is to **sign the image** rather than authenticate the
// transport. The Arduino Update library supports signed images natively: the
// public key is compiled into the firmware and the signature is checked before
// the boot pointer is flipped, so a forged image is rejected even if it arrives
// over plain HTTP from a hostile server. Signatures do not expire, which means
// this also works on a device that has been offline for years.
//
// That would additionally let the ESP8266 drop TLS entirely for the download
// and reclaim ~20 KB of heap. Deferred to Phase 9 because it needs a signing
// key and a release pipeline, which do not exist yet.

}  // namespace beamboy
