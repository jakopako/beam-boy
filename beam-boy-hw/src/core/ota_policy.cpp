#include "core/ota_policy.h"

#include <string.h>

namespace beamboy {
namespace ota {
namespace {

// Exact hosts an image or a redirect may come from. GitHub serves release
// assets from github.com and redirects to a signed URL on a separate asset
// host; both current and previous asset hosts are listed. Anything else --
// including a mirror the manifest itself names -- is refused, so a compromised
// manifest key alone cannot point consoles at an arbitrary server.
const char* const kAllowedHosts[] = {
    "beamboy.ch",
    "github.com",
    "release-assets.githubusercontent.com",
    "objects.githubusercontent.com",
};

char lower(char c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

bool hostEquals(const char* host, size_t length, const char* allowed) {
  if (strlen(allowed) != length) return false;
  for (size_t i = 0; i < length; i++) {
    if (lower(host[i]) != allowed[i]) return false;
  }
  return true;
}

}  // namespace

const char* describe(ReleaseVerdict verdict) {
  switch (verdict) {
    case ReleaseVerdict::kAvailable:
      return "update available";
    case ReleaseVerdict::kUpToDate:
      return "up to date";
    case ReleaseVerdict::kOlder:
      return "published release is older than this firmware";
    case ReleaseVerdict::kWrongChannel:
      return "manifest is for another channel";
    case ReleaseVerdict::kPrerelease:
      return "prereleases are not offered";
    case ReleaseVerdict::kNoImageForBoard:
      return "no image for this board";
    case ReleaseVerdict::kWrongLayout:
      return "release needs a different partition layout (USB update required)";
    case ReleaseVerdict::kNeedsNewerUpdater:
      return "release needs a newer updater; install an intermediate release";
    case ReleaseVerdict::kSaveIncompatible:
      return "release cannot read this console's saves";
    case ReleaseVerdict::kApiIncompatible:
      return "release cannot run the installed games";
    case ReleaseVerdict::kTooLarge:
      return "image does not fit the update slot";
    case ReleaseVerdict::kUntrustedUrl:
      return "image URL is not an allowed https host";
  }
  return "unknown verdict";
}

ReleaseVerdict evaluateRelease(const fw::Release& release,
                               const DeviceIdentity& device,
                               const fw::BoardImage*& image) {
  image = nullptr;

  if (strcmp(release.channel, BEAMBOY_OTA_CHANNEL) != 0) {
    return ReleaseVerdict::kWrongChannel;
  }
  if (fw::isPrerelease(release.version)) return ReleaseVerdict::kPrerelease;

  // Ordering is by sequence alone. An up-to-date console reports exactly that,
  // even if the newest release would also be incompatible with it.
  if (release.sequence == device.sequence) return ReleaseVerdict::kUpToDate;
  if (release.sequence < device.sequence) return ReleaseVerdict::kOlder;

  const fw::BoardImage* entry = nullptr;
  for (uint8_t i = 0; i < release.board_count && i < fw::kMaxBoards; i++) {
    if (strcmp(release.boards[i].board, device.board) == 0) {
      entry = &release.boards[i];
      break;
    }
  }
  if (entry == nullptr) return ReleaseVerdict::kNoImageForBoard;
  if (strcmp(entry->layout, device.layout) != 0) {
    return ReleaseVerdict::kWrongLayout;
  }
  if (entry->min_updater > device.updater_version) {
    return ReleaseVerdict::kNeedsNewerUpdater;
  }
  if (entry->min_save > device.save_version) {
    return ReleaseVerdict::kSaveIncompatible;
  }
  if (entry->min_api > device.api_version) {
    return ReleaseVerdict::kApiIncompatible;
  }
  if (device.slot_size == 0 || entry->size > device.slot_size) {
    return ReleaseVerdict::kTooLarge;
  }
  if (!isAllowedUrl(entry->url)) return ReleaseVerdict::kUntrustedUrl;

  image = entry;
  return ReleaseVerdict::kAvailable;
}

bool isAllowedUrl(const char* url) {
  if (url == nullptr || strncmp(url, "https://", 8) != 0) return false;
  for (const char* p = url; *p != '\0'; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c <= 0x20 || c >= 0x7f) return false;
  }

  // The authority is everything up to the first '/', exactly as arduino-esp32's
  // HTTPClient parses it -- not up to '?' or '#'. Otherwise
  // "https://github.com?@evil.example/" would pass here while HTTPClient reads
  // "github.com?" as userinfo and connects to evil.example. Any userinfo, port,
  // query or fragment character before the first '/' therefore fails the exact
  // host comparison below.
  const char* host = url + 8;
  size_t length = 0;
  while (host[length] != '\0' && host[length] != '/') {
    const char c = host[length];
    if (c == '@' || c == ':' || c == '\\' || c == '?' || c == '#') {
      return false;
    }
    length++;
  }
  if (length == 0) return false;

  for (size_t i = 0; i < sizeof(kAllowedHosts) / sizeof(kAllowedHosts[0]);
       i++) {
    if (hostEquals(host, length, kAllowedHosts[i])) return true;
  }
  return false;
}

bool isRedirectStatus(int http_code) {
  return http_code == 301 || http_code == 302 || http_code == 303 ||
         http_code == 307 || http_code == 308;
}

const char* describe(PowerGate gate) {
  switch (gate) {
    case PowerGate::kOk:
      return "power ok";
    case PowerGate::kUnavailable:
      return "battery reading unavailable";
    case PowerGate::kStale:
      return "battery reading is stale";
    case PowerGate::kTooLow:
      return "battery below 30%; charge before updating";
    case PowerGate::kCritical:
      return "battery critical";
  }
  return "unknown power state";
}

PowerGate installPowerGate(bool reading_available, uint32_t reading_age_ms,
                           float percent, PowerLevel level) {
  if (!reading_available) return PowerGate::kUnavailable;
  // Written to also reject NaN.
  if (!(percent >= 0.0f && percent <= 100.0f)) return PowerGate::kUnavailable;
  if (reading_age_ms > kMaxBatteryReadingAgeMs) return PowerGate::kStale;
  if (level == PowerLevel::kCritical) return PowerGate::kCritical;
  if (level == PowerLevel::kLow || percent < kMinInstallBatteryPercent) {
    return PowerGate::kTooLow;
  }
  return PowerGate::kOk;
}

void ConfirmHold::reset() {
  armed_ = false;
  holding_ = false;
  confirmed_ = false;
  hold_start_ms_ = 0;
}

ConfirmHold::Event ConfirmHold::update(bool a_down, uint32_t now_ms) {
  if (confirmed_) return Event::kNone;

  if (!a_down) {
    armed_ = true;
    if (holding_) {
      holding_ = false;
      return Event::kCancelled;
    }
    return Event::kNone;
  }

  if (!armed_) return Event::kNone;

  if (!holding_) {
    holding_ = true;
    hold_start_ms_ = now_ms;
    return Event::kStarted;
  }

  if (now_ms - hold_start_ms_ >= kConfirmHoldMs) {
    holding_ = false;
    armed_ = false;
    confirmed_ = true;
    return Event::kConfirmed;
  }
  return Event::kNone;
}

float ConfirmHold::progress(uint32_t now_ms) const {
  if (confirmed_) return 1.0f;
  if (!holding_) return 0.0f;
  const uint32_t elapsed = now_ms - hold_start_ms_;
  if (elapsed >= kConfirmHoldMs) return 1.0f;
  return static_cast<float>(elapsed) / static_cast<float>(kConfirmHoldMs);
}

const char* describe(ImageError error) {
  switch (error) {
    case ImageError::kNone:
      return "ok";
    case ImageError::kBadHeader:
      return "not an ESP application image";
    case ImageError::kTooLong:
      return "image longer than signed size";
    case ImageError::kTruncated:
      return "image shorter than signed size";
    case ImageError::kHashMismatch:
      return "image SHA-256 does not match the signed manifest";
  }
  return "unknown image error";
}

void ImageCheck::begin(uint32_t expected_size) {
  expected_ = expected_size;
  received_ = 0;
}

ImageError ImageCheck::accept(const uint8_t* data, size_t length) {
  if (length == 0) return ImageError::kNone;
  if (received_ == 0 && data[0] != kEspImageMagic) {
    return ImageError::kBadHeader;
  }
  if (length > static_cast<size_t>(expected_ - received_)) {
    return ImageError::kTooLong;
  }
  received_ += static_cast<uint32_t>(length);
  return ImageError::kNone;
}

ImageError ImageCheck::finish(const uint8_t* digest,
                              const uint8_t* expected) const {
  if (received_ != expected_) return ImageError::kTruncated;
  uint8_t diff = 0;
  for (size_t i = 0; i < fw::kSha256Bytes; i++) {
    diff |= static_cast<uint8_t>(digest[i] ^ expected[i]);
  }
  return diff == 0 ? ImageError::kNone : ImageError::kHashMismatch;
}

float ImageCheck::progress() const {
  if (expected_ == 0) return 0.0f;
  return static_cast<float>(received_) / static_cast<float>(expected_);
}

}  // namespace ota
}  // namespace beamboy
