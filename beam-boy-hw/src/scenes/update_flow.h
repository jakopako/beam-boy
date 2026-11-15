#pragma once

// Beam Boy — the firmware-update screen's state machine, free of WiFi, flash
// and rendering so the host tests can walk every path.
//
// UpdateFirmwareScene owns Network and Ota and does the I/O; this class only
// decides what happens next. The scene feeds it one Frame per tick plus the
// result of every operation it runs, and does whatever Action comes back.
//
//   enter -> no credentials (stop; Settings > WiFi)
//         -> connecting -> checking -> up to date
//                                   -> available --hold A 1.2 s--> downloading
//                                   -> staged (installed, awaiting restart)
//                                   -> failed (connect/check/incompatible)
//   downloading -> verifying -> success --A--> reboot
//   failed --A--> start again from connecting
//
// Hold B is the engine's utility exit and is never seen here; the scene's
// exit() cancels whatever is running. B is never reused for reboot.

#include <stdint.h>

#include "core/ota_policy.h"

namespace beamboy {
namespace update {

enum class Stage : uint8_t {
  kNoCredentials,
  kConnecting,
  kChecking,
  kUpToDate,
  kAvailable,
  kDownloading,
  kVerifying,
  kSuccess,  // Installed in this visit; A restarts into it.
  kStaged,   // Installed earlier and not yet restarted into; A restarts.
  kFailed,
};

// Why kFailed was reached. Each gets its own rhythm on the tube.
enum class Failure : uint8_t {
  kNone,
  kConnect,       // WiFi did not connect, or dropped.
  kCheck,         // Manifest fetch, signature or format.
  kIncompatible,  // A newer release exists but cannot be installed here.
  kPower,         // Battery too low, stale or unreadable.
  kInstall,       // Saves not clean, download, verify or slot selection.
};

const char* describe(Stage stage);
const char* describe(Failure failure);

enum class CheckOutcome : uint8_t {
  kAvailable,
  kUpToDate,
  kIncompatible,
  kStaged,
  kFailed,
};

enum class InstallOutcome : uint8_t {
  kDownloading,
  kVerifying,
  kSuccess,
  kFailed,
};

// What the scene must do after Flow::update().
enum class Action : uint8_t {
  kNone,
  kStart,    // (Re)load credentials and connect, then call start().
  kCheck,    // Run Ota::check() and report onCheck().
  kInstall,  // Check preconditions, Ota::beginInstall(), onInstallStarted().
  kReboot,   // Restart into the selected image.
};

struct Frame {
  uint32_t now_ms;
  bool a_down;
  bool a_pressed;  // Went down this frame.
  bool net_connected;
  bool net_failed;
};

class Flow {
 public:
  // Called on scene entry and on every retry. Clears everything from before,
  // including any confirmation progress or reboot arming.
  void start(bool has_credentials, uint32_t now_ms);

  Action update(const Frame& frame);

  void onCheck(CheckOutcome outcome, uint32_t now_ms);
  // `refusal` says why when !started: kPower or kInstall.
  void onInstallStarted(bool started, Failure refusal, uint32_t now_ms);
  void onInstall(InstallOutcome outcome, uint32_t now_ms);

  // The scene stopped the radio and any install behind the flow's back (a
  // critical battery). Anything still in progress becomes a failure that A
  // can retry; finished results (up to date, installed) stay as they are.
  void abort(Failure failure, uint32_t now_ms);

  Stage stage() const { return stage_; }
  Failure failure() const { return failure_; }
  uint32_t stageMs(uint32_t now_ms) const { return now_ms - stage_since_ms_; }

  // 0..1 while A is being held to confirm an install.
  float confirmProgress(uint32_t now_ms) const {
    return confirm_.progress(now_ms);
  }

  // The radio is wanted only while a result is still being worked towards.
  // Every result stage switches it off.
  bool radioNeeded() const;

  // Flash is being written; the engine must not sleep.
  bool installing() const {
    return stage_ == Stage::kDownloading || stage_ == Stage::kVerifying;
  }

 private:
  void enterStage(Stage stage, uint32_t now_ms);
  void fail(Failure failure, uint32_t now_ms);

  Stage stage_ = Stage::kNoCredentials;
  Failure failure_ = Failure::kNone;
  uint32_t stage_since_ms_ = 0;
  ota::ConfirmHold confirm_;
  // A reboot needs A released after the result appeared, then pressed again,
  // so the hold that confirmed the install can never also restart.
  bool reboot_armed_ = false;
};

}  // namespace update
}  // namespace beamboy
