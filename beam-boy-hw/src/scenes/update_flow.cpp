#include "scenes/update_flow.h"

namespace beamboy {
namespace update {

const char* describe(Stage stage) {
  switch (stage) {
    case Stage::kNoCredentials:
      return "no WiFi credentials";
    case Stage::kConnecting:
      return "connecting";
    case Stage::kChecking:
      return "checking";
    case Stage::kUpToDate:
      return "up to date";
    case Stage::kAvailable:
      return "update available";
    case Stage::kDownloading:
      return "downloading";
    case Stage::kVerifying:
      return "verifying";
    case Stage::kSuccess:
      return "installed";
    case Stage::kStaged:
      return "installed, awaiting restart";
    case Stage::kFailed:
      return "failed";
  }
  return "unknown";
}

const char* describe(Failure failure) {
  switch (failure) {
    case Failure::kNone:
      return "none";
    case Failure::kConnect:
      return "connection";
    case Failure::kCheck:
      return "update check";
    case Failure::kIncompatible:
      return "incompatible release";
    case Failure::kPower:
      return "power";
    case Failure::kInstall:
      return "install";
  }
  return "unknown";
}

void Flow::enterStage(Stage stage, uint32_t now_ms) {
  stage_ = stage;
  stage_since_ms_ = now_ms;
  confirm_.reset();
  reboot_armed_ = false;
}

void Flow::fail(Failure failure, uint32_t now_ms) {
  failure_ = failure;
  enterStage(Stage::kFailed, now_ms);
}

void Flow::start(bool has_credentials, uint32_t now_ms) {
  failure_ = Failure::kNone;
  enterStage(has_credentials ? Stage::kConnecting : Stage::kNoCredentials,
             now_ms);
}

bool Flow::radioNeeded() const {
  switch (stage_) {
    case Stage::kConnecting:
    case Stage::kChecking:
    case Stage::kAvailable:
    case Stage::kDownloading:
    case Stage::kVerifying:
      return true;
    default:
      return false;
  }
}

Action Flow::update(const Frame& frame) {
  switch (stage_) {
    case Stage::kConnecting:
      if (frame.net_connected) {
        enterStage(Stage::kChecking, frame.now_ms);
        return Action::kCheck;
      }
      if (frame.net_failed) fail(Failure::kConnect, frame.now_ms);
      return Action::kNone;

    case Stage::kAvailable:
      // Losing WiFi while waiting for confirmation would only fail later,
      // mid-request; say so now.
      if (frame.net_failed) {
        fail(Failure::kConnect, frame.now_ms);
        return Action::kNone;
      }
      if (confirm_.update(frame.a_down, frame.now_ms) ==
          ota::ConfirmHold::Event::kConfirmed) {
        return Action::kInstall;
      }
      return Action::kNone;

    case Stage::kFailed:
      return frame.a_pressed ? Action::kStart : Action::kNone;

    case Stage::kSuccess:
    case Stage::kStaged:
      if (!frame.a_down) {
        reboot_armed_ = true;
        return Action::kNone;
      }
      return reboot_armed_ && frame.a_pressed ? Action::kReboot : Action::kNone;

    default:
      return Action::kNone;
  }
}

void Flow::onCheck(CheckOutcome outcome, uint32_t now_ms) {
  if (stage_ != Stage::kChecking) return;
  switch (outcome) {
    case CheckOutcome::kAvailable:
      enterStage(Stage::kAvailable, now_ms);
      break;
    case CheckOutcome::kUpToDate:
      enterStage(Stage::kUpToDate, now_ms);
      break;
    case CheckOutcome::kStaged:
      enterStage(Stage::kStaged, now_ms);
      break;
    case CheckOutcome::kIncompatible:
      fail(Failure::kIncompatible, now_ms);
      break;
    case CheckOutcome::kFailed:
      fail(Failure::kCheck, now_ms);
      break;
  }
}

void Flow::onInstallStarted(bool started, Failure refusal, uint32_t now_ms) {
  if (stage_ != Stage::kAvailable) return;
  if (started) {
    enterStage(Stage::kDownloading, now_ms);
    return;
  }
  fail(refusal == Failure::kPower ? Failure::kPower : Failure::kInstall,
       now_ms);
}

void Flow::onInstall(InstallOutcome outcome, uint32_t now_ms) {
  if (!installing()) return;
  switch (outcome) {
    case InstallOutcome::kDownloading:
      break;
    case InstallOutcome::kVerifying:
      if (stage_ != Stage::kVerifying) enterStage(Stage::kVerifying, now_ms);
      break;
    case InstallOutcome::kSuccess:
      enterStage(Stage::kSuccess, now_ms);
      break;
    case InstallOutcome::kFailed:
      fail(Failure::kInstall, now_ms);
      break;
  }
}

void Flow::abort(Failure failure, uint32_t now_ms) {
  if (radioNeeded()) fail(failure, now_ms);
}

}  // namespace update
}  // namespace beamboy
