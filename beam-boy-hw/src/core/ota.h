#pragma once

// Beam Boy — firmware over-the-air updates.
//
// This updates the *engine*, not the games. Two different things, easily
// confused:
//
//   Games      -> scripts + metadata written to the LittleFS data partition.
//                 No reboot. A bad one breaks that game only.
//   Firmware   -> this file. Replaces the executable itself: renderer, input,
//                 launcher, network code. Requires a reboot. A bad one bricks
//                 the console.
//
// Because the downside is bricking, the mechanism is deliberately conservative.
// Flash is divided into two application slots. The device runs from slot A and
// writes the download into slot B, which is not executable until it is complete
// and verified. Only then is the boot pointer flipped. If the download stalls,
// the checksum fails, or the power drops halfway, slot A is untouched and the
// console boots exactly as before. There is no state in which a partial
// download can run.
//
// The Arduino Update library implements the slot handling; what this class adds
// is the frame-loop-friendly shape (never block the tube), progress reporting,
// and a version check so the device does not reinstall what it already runs.

#include <Arduino.h>

namespace beamboy {

enum class OtaState : uint8_t {
  kIdle,
  kChecking,     // Fetching the version manifest.
  kAvailable,    // Manifest names a different version; ready to install.
  kUpToDate,     // Manifest says we already run the newest build.
  kDownloading,  // Streaming the image into the spare slot.
  kSuccess,      // Written and verified; awaiting reboot.
  kFailed,
};

class Ota {
 public:
  // Asks the update server what the current version is. Requires an active
  // network connection; returns false immediately if there is none.
  bool checkForUpdate();

  // Downloads and installs the image discovered by checkForUpdate().
  //
  // NOTE: unlike the rest of the engine this call blocks for the duration of
  // the download -- typically several seconds. It is safe here, and only here,
  // because the tube cannot animate during a flash write anyway: writing to
  // flash disables interrupts in bursts, which corrupts the WS2812 signal. The
  // scene therefore paints a static "working" pattern before calling this, and
  // the watchdog is fed by the Update library's internal yields.
  bool install();

  OtaState state() const { return state_; }

  // 0..1 while downloading, for the progress bar on the tube.
  float progress() const { return progress_; }

  // Human-readable reason for kFailed, printed to serial. The tube can only say
  // "it failed", so this is where the detail lives.
  const char* error() const { return error_; }

  const char* availableVersion() const { return available_; }

  // The version this firmware was built as. Compared against the manifest.
  static const char* currentVersion();

 private:
  void fail(const char* reason);

  OtaState state_ = OtaState::kIdle;
  float progress_ = 0.0f;
  const char* error_ = "";
  char available_[24] = {0};
  char image_url_[160] = {0};
};

}  // namespace beamboy
