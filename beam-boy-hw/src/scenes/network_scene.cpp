#include "network_scene.h"

#include "../core/board_config.h"

namespace beamboy {
namespace {

// Each state gets its own colour AND its own motion -- see the header for why
// colour alone is insufficient.
const Color kConnectColor(0, 160, 255);
const Color kSetupColor(255, 140, 0);
const Color kForgetColor(255, 40, 30);
const Color kOkColor(0, 255, 60);
const Color kFailColor(255, 30, 20);
const Color kPortalColor(255, 150, 20);

constexpr float kSweepSpeed = 1.6f;   // Connecting sweep, strip-lengths/sec.
constexpr float kPulseSpeed = 0.9f;   // Portal breathing, cycles/sec.
constexpr uint32_t kSettleMs = 2500;  // Flash duration before settling.

}  // namespace

void NetworkScene::enter(Engine& engine) {
  (void)engine;
  net_.begin();

  // Offer the most likely action first: connect if we know a network, set one
  // up if we do not.
  menu_ = net_.hasCredentials() ? Menu::kConnect : Menu::kSetup;
  phase_ = 0.0f;
  settled_at_ms_ = 0;
  ota_active_ = false;
  last_state_ = NetState::kOff;
  forget_armed_ = false;
  forgot_at_ms_ = 0;
}

void NetworkScene::exit(Engine& engine) {
  (void)engine;

  // Leaving the scene always takes the radio down. Anything else would let a
  // user wander back to the launcher with WiFi silently draining the battery,
  // which is exactly the failure the opt-in design exists to prevent.
  net_.disconnect();
}

void NetworkScene::update(Engine& engine, float dt) {
  Input& input = engine.input();

  net_.tick();
  phase_ += dt;

  const NetState state = net_.state();
  if (state != last_state_) {
    last_state_ = state;
    settled_at_ms_ = (state == NetState::kConnected || state == NetState::kFailed)
                         ? millis()
                         : 0;
    if (state == NetState::kPortalActive) {
      Serial.print(F("[net] portal open -- join '"));
      Serial.print(F("BeamBoy-Setup' then open http://"));
      Serial.println(net_.address());
    } else if (state == NetState::kConnected) {
      Serial.print(F("[net] connected, ip "));
      Serial.print(net_.address());
      // TLS needs ~16-22 KB for a handshake on the ESP8266. If this number is
      // close to that, OTA will fail at the handshake rather than the download,
      // which looks identical from the tube -- so it is worth logging.
      Serial.print(F("  free heap "));
      Serial.println(ESP.getFreeHeap());
    }

    // Any state change invalidates an OTA result being displayed.
    if (state != NetState::kConnected) ota_active_ = false;
  }

  // While the radio is busy, B cancels and returns to the menu. A is ignored so
  // a stray press cannot restart an operation that is already running.
  if (state != NetState::kOff) {
    // Once connected, A starts a firmware update. This is the only route to
    // OTA, so it cannot be reached without deliberately connecting first.
    if (state == NetState::kConnected && !ota_active_ && input.pressed(Button::kA)) {
      ota_active_ = true;
      phase_ = 0.0f;  // The OTA animations are timed from here, not scene entry.

      // Paint the "working" frame before starting, because the download blocks:
      // flash writes disable interrupts in bursts and would corrupt the WS2812
      // signal anyway, so the tube must be left showing something sensible.
      Display& display = engine.display();
      display.clear();
      display.span(0.0f, 1.0f, Color(255, 150, 20), 0.3f);
      display.present();

      if (ota_.checkForUpdate()) ota_.install();
      return;
    }

    if (input.pressed(Button::kB)) {
      if (ota_active_) {
        // A staged update does nothing until the device restarts. Rather than
        // rebooting out from under the player, the success state waits here and
        // B performs the restart -- so the reboot is always a deliberate act.
        if (ota_.state() == OtaState::kSuccess) {
          Serial.println(F("[net] rebooting into new firmware"));
          Serial.flush();
          ESP.restart();
        }
        // Any other OTA result is just acknowledged.
        ota_active_ = false;
      } else {
        net_.disconnect();
      }
    }
    return;
  }

  const int8_t step = input.navDelta();
  if (step != 0) {
    // Which entries exist depends on whether credentials are stored: there is
    // nothing to forget if we have never been provisioned.
    if (net_.hasCredentials()) {
      int8_t index = static_cast<int8_t>(menu_) + step;
      if (index < 0) index = 0;
      if (index > static_cast<int8_t>(Menu::kForget)) index = static_cast<int8_t>(Menu::kForget);
      menu_ = static_cast<Menu>(index);
    } else {
      menu_ = Menu::kSetup;
    }
    // Moving away from Forget cancels it. Leaving it armed would mean a later,
    // unrelated press on that entry erases immediately with no warning.
    forget_armed_ = false;
  }

  // An armed Forget also expires on its own, so it cannot sit primed
  // indefinitely waiting to catch a press the user has forgotten the context of.
  if (forget_armed_ && millis() - forget_armed_at_ms_ > kForgetArmMs) {
    forget_armed_ = false;
  }

  if (input.pressed(Button::kA) || input.pressed(Input::kNavButton)) {
    applyMenu(engine);
  }
}

void NetworkScene::applyMenu(Engine& engine) {
  switch (menu_) {
    case Menu::kConnect:
      net_.connect();
      break;
    case Menu::kSetup: {
      // Bringing the AP up still takes a moment even though the scan is now
      // asynchronous, so paint a frame first rather than letting the menu sit
      // frozen mid-press.
      Display& display = engine.display();
      display.clear();
      display.span(0.0f, 1.0f, Color(255, 150, 20), 0.25f);
      display.present();
      net_.startPortal();
      break;
    }
    case Menu::kForget:
      // Two presses, because this is destructive and there is no text to warn
      // with. The first arms it (the entry starts flashing urgently), the second
      // within kForgetArmMs commits. Navigating away disarms.
      if (!forget_armed_) {
        forget_armed_ = true;
        forget_armed_at_ms_ = millis();
        break;
      }
      net_.forget();
      forget_armed_ = false;
      forgot_at_ms_ = millis();
      menu_ = Menu::kSetup;
      Serial.println(F("[net] credentials erased"));
      break;
  }
}

void NetworkScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  if (net_.state() == NetState::kOff) {
    drawMenu(engine);
  } else if (ota_active_) {
    drawOta(engine);
  } else {
    drawStatus(engine);
  }
  (void)display;
}

void NetworkScene::drawOta(Engine& engine) {
  Display& display = engine.display();

  switch (ota_.state()) {
    case OtaState::kSuccess: {
      // Green fills from both ends and meets in the middle, then pulses and
      // holds. The pulse is deliberate: the update is staged but NOT running
      // until the device restarts, so this must not look like a finished,
      // dismissable state. B reboots.
      const float t = fminf(phase_ * 0.5f, 1.0f);
      display.span(0.0f, t * 0.5f, kOkColor, 0.9f);
      display.span(1.0f - t * 0.5f, 1.0f, kOkColor, 0.9f);
      if (t >= 1.0f) {
        const float pulse = 0.5f + 0.5f * sinf(phase_ * 5.0f);
        display.span(0.45f, 0.55f, colors::kWhite, pulse);
      }
      break;
    }

    case OtaState::kUpToDate: {
      // Calm steady blue -- nothing happened, nothing is wrong.
      display.span(0.0f, 1.0f, kConnectColor, 0.2f);
      break;
    }

    case OtaState::kFailed: {
      display.span(0.0f, 1.0f, kFailColor, 0.35f);
      break;
    }

    default: {
      // Downloading. This frame is only ever seen as the static image painted
      // before install() blocks, so it must read as "busy" without animation.
      display.span(0.0f, ota_.progress(), kPortalColor, 0.5f);
      break;
    }
  }
}

void NetworkScene::drawMenu(Engine& engine) {
  Display& display = engine.display();

  // Acknowledge a completed forget before drawing the menu. Without this the
  // only evidence is the menu quietly having one fewer entry, which is not
  // enough to tell "it worked" from "the button did nothing".
  if (forgot_at_ms_ != 0) {
    const uint32_t since = millis() - forgot_at_ms_;
    if (since < kForgotFlashMs) {
      // A red wipe inward from both ends: something was removed.
      const float t = static_cast<float>(since) / kForgotFlashMs;
      display.span(t * 0.5f, 1.0f - t * 0.5f, kForgetColor, 0.9f - t * 0.5f);
      return;
    }
    forgot_at_ms_ = 0;
  }

  // The menu is a short bar whose colour names the action. With no text, colour
  // plus position is the whole vocabulary, so entries sit at fixed positions:
  // the same action is always in the same place.
  struct Item {
    Menu menu;
    Color color;
    float center;
  };

  const bool provisioned = net_.hasCredentials();
  const Item items[] = {
      {Menu::kConnect, kConnectColor, 0.2f},
      {Menu::kSetup, kSetupColor, 0.5f},
      {Menu::kForget, kForgetColor, 0.8f},
  };

  for (const Item& item : items) {
    if (!provisioned && item.menu != Menu::kSetup) continue;

    const bool selected = item.menu == menu_;
    // Every entry is drawn the same width, and only brightness plus motion marks
    // the selection. An earlier version drew unselected entries a third of the
    // width, which on a short strip made them single dim pixels of three
    // different colours -- indistinguishable from rendering artefacts rather
    // than reading as a row of deliberate choices.
    float breathe = selected ? 0.65f + 0.35f * sinf(phase_ * 4.0f) : 0.10f;

    // An armed Forget blinks hard and fast rather than breathing calmly, so the
    // "press again and I erase" state cannot be mistaken for the resting one.
    if (selected && item.menu == Menu::kForget && forget_armed_) {
      breathe = sinf(phase_ * 18.0f) > 0.0f ? 1.0f : 0.05f;
    }

    const float half = display.pixelWidth() * 1.5f;

    display.span(item.center - half, item.center + half, item.color, breathe);
  }
}

void NetworkScene::drawStatus(Engine& engine) {
  Display& display = engine.display();

  switch (net_.state()) {
    case NetState::kConnecting: {
      // A dot sweeping the full length: unmistakably "working", and its travel
      // doubles as a rough progress indication.
      const float head = fmodf(phase_ * kSweepSpeed, 1.0f);
      display.point(head, kConnectColor);
      display.point(head - display.pixelWidth(), kConnectColor, 0.5f);
      display.point(head - display.pixelWidth() * 2.0f, kConnectColor, 0.2f);
      break;
    }

    case NetState::kPortalActive:
    case NetState::kPortalSaved: {
      // Slow amber breathing across the whole tube -- deliberately calm and
      // unhurried, because the user is meant to be looking at their phone, not
      // at the console.
      const float level = 0.25f + 0.35f * sinf(phase_ * kPulseSpeed * 6.283f);
      display.span(0.0f, 1.0f, kPortalColor, level);

      // A scan is invisible from the tube otherwise, and it is the one moment
      // the user may be waiting on the console rather than on their phone. A
      // brighter dot sweeping the length says "working" without disturbing the
      // portal's own calm background.
      if (net_.scanning()) {
        const float head = fmodf(phase_ * kSweepSpeed, 1.0f);
        display.point(head, colors::kWhite, 0.7f);
      }
      break;
    }

    case NetState::kConnected: {
      const uint32_t since = millis() - settled_at_ms_;
      if (since < kSettleMs) {
        // Celebrate briefly: a green wipe outward from the centre.
        const float t = static_cast<float>(since) / kSettleMs;
        display.span(0.5f - t * 0.5f, 0.5f + t * 0.5f, kOkColor, 1.0f - t * 0.5f);
      } else {
        // Then settle to a calm heartbeat so the tube is not a lamp.
        const float level = 0.12f + 0.08f * sinf(phase_ * 2.0f);
        display.span(0.0f, 1.0f, kOkColor, level);
      }
      break;
    }

    case NetState::kFailed: {
      const uint32_t since = millis() - settled_at_ms_;
      if (since < kSettleMs) {
        // Three sharp flashes: distinct from the connected wipe by rhythm as
        // well as by colour.
        const float t = static_cast<float>(since) / kSettleMs;
        const float flash = sinf(t * 3.0f * 6.283f);
        if (flash > 0.0f) display.span(0.0f, 1.0f, kFailColor, flash);
      } else {
        display.span(0.0f, 1.0f, kFailColor, 0.1f);
      }
      break;
    }

    case NetState::kOff:
      break;
  }
}

}  // namespace beamboy
