#include "scenes/update_firmware_scene.h"

#include "core/firmware_identity.h"
#include "core/ota_policy.h"
#include "scenes/update_firmware_view.h"

namespace beamboy {
namespace {

update::CheckOutcome checkOutcome(OtaState state) {
  switch (state) {
    case OtaState::kAvailable:
      return update::CheckOutcome::kAvailable;
    case OtaState::kUpToDate:
      return update::CheckOutcome::kUpToDate;
    case OtaState::kIncompatible:
      return update::CheckOutcome::kIncompatible;
    case OtaState::kStaged:
      return update::CheckOutcome::kStaged;
    default:
      return update::CheckOutcome::kFailed;
  }
}

update::InstallOutcome installOutcome(OtaState state) {
  switch (state) {
    case OtaState::kDownloading:
      return update::InstallOutcome::kDownloading;
    case OtaState::kVerifying:
      return update::InstallOutcome::kVerifying;
    case OtaState::kSuccess:
      return update::InstallOutcome::kSuccess;
    default:
      return update::InstallOutcome::kFailed;
  }
}

}  // namespace

void UpdateFirmwareScene::enter(Engine& engine) {
  // The WiFi stack needs servicing between frames; see
  // Engine::setIdleServiced().
  engine.setIdleServiced(true);
  logged_any_ = false;
  // Not fw::kBuildTag: each file that uses it embeds its own copy, and the
  // release tool requires exactly one tag in an image.
  Serial.print(F("[update] running "));
  Serial.print(fw::kVersion);
  Serial.print(F(" (sequence "));
  Serial.print(fw::kSequence);
  Serial.println(F(")"));
  start();
}

void UpdateFirmwareScene::start() {
  ota_.reset();
  // Reloads credentials and makes sure the radio starts from off, so a retry
  // behaves exactly like a fresh entry.
  net_.begin();
  const bool has_credentials = net_.hasCredentials();
  flow_.start(has_credentials, millis());
  if (!has_credentials) {
    Serial.println(
        F("[update] no WiFi credentials; set up WiFi in Settings > WiFi "
          "first"));
    return;
  }
  Serial.println(F("[update] connecting"));
  net_.connect();
}

void UpdateFirmwareScene::stopEverything(Engine& engine) {
  // Cancelling aborts the slot write without selecting it; the running
  // firmware is untouched.
  if (ota_.busy()) ota_.cancel();
  ota_.reset();
  net_.disconnect();
  engine.setSleepInhibited(false);
}

void UpdateFirmwareScene::exit(Engine& engine) {
  if (ota_.busy()) {
    Serial.println(F("[update] left during install; cancelled"));
  }
  stopEverything(engine);
}

void UpdateFirmwareScene::abortForShutdown(Engine& engine) {
  if (ota_.busy()) {
    Serial.println(F("[update] critical battery; install cancelled"));
  }
  stopEverything(engine);
  // The engine resumes this scene if USB is plugged in during the shutdown
  // sweep; it must then show a retryable failure, not a flow still waiting
  // on a radio that was just switched off.
  flow_.abort(update::Failure::kPower, millis());
}

void UpdateFirmwareScene::idle(Engine& engine) {
  (void)engine;
  net_.tick();
}

void UpdateFirmwareScene::paint(Engine& engine, update::Stage stage) {
  const update::View view = {stage, update::Failure::kNone, 0, 0.0f, 0.0f};
  update::render(engine.display(), view);
  engine.display().present();
}

void UpdateFirmwareScene::runCheck(Engine& engine) {
  // Ota::check() blocks for a TLS handshake and a small download; show a
  // steady frame rather than a frozen animation.
  paint(engine, update::Stage::kChecking);
  Serial.println(F("[update] checking for a new release"));
  const OtaState state = ota_.check();
  flow_.onCheck(checkOutcome(state), millis());
}

void UpdateFirmwareScene::runInstall(Engine& engine) {
  const uint32_t now_ms = millis();
  Power& power = engine.power();
  // The battery's own level, not the USB-overridden alert level: a cable can
  // be pulled half-way through.
  const ota::PowerGate gate =
      ota::installPowerGate(power.available(), power.readingAgeMs(now_ms),
                            power.percent(), power.level());
  if (gate != ota::PowerGate::kOk) {
    Serial.print(F("[update] not installing: "));
    Serial.println(ota::describe(gate));
    flow_.onInstallStarted(false, update::Failure::kPower, now_ms);
    return;
  }

  // Anything still pending is written now; a save that cannot be written
  // blocks the update rather than being lost to a restart.
  Storage& storage = engine.storage();
  storage.commit();
  const InstallPreconditions preconditions = {
      gate, storage.mounted() && !storage.dirty()};

  // Opening the image stream blocks for TLS and redirects.
  paint(engine, update::Stage::kDownloading);
  const bool started = ota_.beginInstall(preconditions);
  flow_.onInstallStarted(started, update::Failure::kInstall, millis());
}

void UpdateFirmwareScene::reboot(Engine& engine) {
  Serial.println(F("[update] restarting into the new firmware"));
  Serial.flush();
  net_.disconnect();
  engine.display().clear();
  engine.display().present();
  delay(100);
  ESP.restart();
}

void UpdateFirmwareScene::logStageChange() {
  const update::Stage stage = flow_.stage();
  if (logged_any_ && stage == logged_stage_) return;
  logged_any_ = true;
  logged_stage_ = stage;
  Serial.print(F("[update] "));
  Serial.print(update::describe(stage));
  if (stage == update::Stage::kFailed) {
    Serial.print(F(" ("));
    Serial.print(update::describe(flow_.failure()));
    if (ota_.error()[0] != '\0') {
      Serial.print(F(": "));
      Serial.print(ota_.error());
    }
    Serial.print(F(") -- A retries, hold B returns"));
  } else if (stage == update::Stage::kAvailable) {
    Serial.print(F(": "));
    Serial.print(Ota::currentVersion());
    Serial.print(F(" -> "));
    Serial.print(ota_.availableVersion());
    Serial.print(F(" -- hold A to install"));
  } else if (stage == update::Stage::kSuccess ||
             stage == update::Stage::kStaged) {
    Serial.print(
        F(" -- press A to restart now; it also runs after the next restart"));
  }
  Serial.println();
}

void UpdateFirmwareScene::update(Engine& engine, float dt) {
  (void)dt;
  net_.tick();

  Input& input = engine.input();
  const NetState net_state = net_.state();
  const update::Frame frame = {
      millis(),
      input.held(Button::kA),
      input.pressed(Button::kA),
      net_state == NetState::kConnected,
      net_state == NetState::kFailed,
  };

  switch (flow_.update(frame)) {
    case update::Action::kStart:
      start();
      break;
    case update::Action::kCheck:
      runCheck(engine);
      break;
    case update::Action::kInstall:
      runInstall(engine);
      break;
    case update::Action::kReboot:
      reboot(engine);
      break;
    case update::Action::kNone:
      break;
  }

  // One bounded chunk per frame, so progress is real and hold B (handled by
  // the engine before this runs) cancels between chunks.
  if (flow_.installing()) {
    flow_.onInstall(installOutcome(ota_.poll()), millis());
  }

  engine.setSleepInhibited(flow_.installing());
  if (!flow_.radioNeeded() && net_.state() != NetState::kOff) {
    net_.disconnect();
  }
  logStageChange();
}

void UpdateFirmwareScene::render(Engine& engine) {
  const uint32_t now_ms = millis();
  const update::View view = {
      flow_.stage(),
      flow_.failure(),
      flow_.stageMs(now_ms),
      ota_.progress(),
      flow_.confirmProgress(now_ms),
  };
  update::render(engine.display(), view);
}

}  // namespace beamboy
