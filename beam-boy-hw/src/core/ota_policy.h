#pragma once

// Beam Boy — firmware update decisions, kept free of WiFi, flash and crypto.
//
// Same split as net_policy.h and power_policy.h: Ota (ota.h) does the I/O,
// and every question whose wrong answer bricks a console or installs the wrong
// image is answered here, where the host tests can enumerate it:
//
//   * evaluateRelease()    -- is this signed release for us, and newer?
//   * isAllowedUrl()       -- may the image (or a redirect) come from here?
//   * installPowerGate()   -- is there enough battery to start writing?
//   * ConfirmHold          -- did the player deliberately hold A to install?
//   * ImageCheck           -- did the stream deliver exactly the signed image?

#include <stddef.h>
#include <stdint.h>

#include "core/fw_manifest.h"
#include "core/power_policy.h"

namespace beamboy {
namespace ota {

// The only channel this firmware follows. Prereleases are never auto-offered.
#define BEAMBOY_OTA_CHANNEL "stable"

// What the running firmware is, as far as release selection cares. Built from
// fw:: identity constants on the device, or by hand in a test.
struct DeviceIdentity {
  const char* board;
  const char* layout;
  uint32_t sequence;
  uint16_t updater_version;
  uint16_t save_version;
  uint16_t api_version;
  // Size of the inactive application slot, in bytes.
  uint32_t slot_size;
};

enum class ReleaseVerdict : uint8_t {
  kAvailable,
  kUpToDate,           // Same release sequence.
  kOlder,              // Lower sequence: never a downgrade.
  kWrongChannel,
  kPrerelease,
  kNoImageForBoard,
  kWrongLayout,        // Needs a USB migration, not an OTA.
  kNeedsNewerUpdater,  // Install an intermediate release first.
  kSaveIncompatible,   // New firmware cannot read this console's saves.
  kApiIncompatible,    // New firmware cannot run the installed cartridges.
  kTooLarge,           // Image does not fit the inactive slot.
  kUntrustedUrl,
};

const char* describe(ReleaseVerdict verdict);

// Decides whether `release` should be offered. On kAvailable, `image` points at
// this board's entry inside `release`; otherwise it is null.
ReleaseVerdict evaluateRelease(const fw::Release& release,
                               const DeviceIdentity& device,
                               const fw::BoardImage*& image);

// --- Transport -------------------------------------------------------------

// GitHub release downloads answer with a redirect to a separate asset host, so
// a handful of hops is normal; more means something is wrong.
constexpr uint8_t kMaxRedirects = 5;

// True for an absolute https:// URL whose host is exactly one of the allowed
// download hosts, with no userinfo and no explicit port. Matching is exact,
// not by suffix: "evilgithub.com" and "github.com.evil" are both refused.
bool isAllowedUrl(const char* url);

bool isRedirectStatus(int http_code);

// --- Power -----------------------------------------------------------------

constexpr float kMinInstallBatteryPercent = 30.0f;
// The gauge is polled once a second; anything older than this is stale.
constexpr uint32_t kMaxBatteryReadingAgeMs = 5000;

enum class PowerGate : uint8_t {
  kOk,
  kUnavailable,  // No believable fuel-gauge reading.
  kStale,
  kTooLow,
  kCritical,
};

const char* describe(PowerGate gate);

// Whether a write may start. USB presence and charge state are deliberately
// not inputs: a cable can be pulled mid-write, so it never bypasses the
// threshold. `level` must be the battery's own classification (Power::level()),
// not the USB-overridden alert level. A missing gauge never counts as "no
// battery to worry about": no reading means no update.
PowerGate installPowerGate(bool reading_available, uint32_t reading_age_ms,
                           float percent, PowerLevel level);

// --- Confirmation ----------------------------------------------------------

constexpr uint32_t kConfirmHoldMs = 1200;

// Hold-A-to-install. A press that was already down when the scene opened (the
// one that selected "Update firmware", say) must not count, so A has to be
// seen released before a hold can start. Releasing early cancels.
class ConfirmHold {
 public:
  enum class Event : uint8_t { kNone, kStarted, kCancelled, kConfirmed };

  // Call on scene entry and after every terminal outcome.
  void reset();

  // Feed the current A level once per frame.
  Event update(bool a_down, uint32_t now_ms);

  bool holding() const { return holding_; }
  bool confirmed() const { return confirmed_; }

  // 0..1 while holding, for the overlay.
  float progress(uint32_t now_ms) const;

 private:
  bool armed_ = false;
  bool holding_ = false;
  bool confirmed_ = false;
  uint32_t hold_start_ms_ = 0;
};

// --- Image stream ----------------------------------------------------------

enum class ImageError : uint8_t {
  kNone,
  kBadHeader,     // First byte is not an ESP application image.
  kTooLong,       // More bytes than signed.
  kTruncated,     // Fewer bytes than signed.
  kHashMismatch,
};

const char* describe(ImageError error);

// First byte of every ESP-IDF application image.
constexpr uint8_t kEspImageMagic = 0xE9;

// Accounts for the bytes of a download against the signed size and digest.
// The SHA-256 itself is computed by the caller (mbedTLS on the device) so this
// stays host-testable; finish() compares in constant time.
class ImageCheck {
 public:
  void begin(uint32_t expected_size);

  // Call for each chunk *before* writing it to flash. Rejects a bad header on
  // the first chunk and any byte beyond the signed size.
  ImageError accept(const uint8_t* data, size_t length);

  ImageError finish(const uint8_t* digest, const uint8_t* expected) const;

  uint32_t received() const { return received_; }
  uint32_t expected() const { return expected_; }
  float progress() const;

 private:
  uint32_t expected_ = 0;
  uint32_t received_ = 0;
};

}  // namespace ota
}  // namespace beamboy
