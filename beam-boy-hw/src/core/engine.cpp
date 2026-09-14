#include "engine.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <math.h>

#include "cartridge_store.h"
#include "game_registry.h"

namespace beamboy {
namespace {

// Score bits are coloured by nibble so place values can be read without
// counting pixels: bits 0-3 one colour, 4-7 the next, and so on.
const Color kNibbleColors[] = {
    Color(0, 150, 255),    // bits 0-3   blue
    Color(0, 220, 80),     // bits 4-7   green
    Color(255, 170, 0),    // bits 8-11  amber
    Color(255, 40, 90),    // bits 12-15 red
    Color(190, 90, 255),   // bits 16-19 violet
    Color(255, 255, 255),  // bits 20+   white
};
constexpr uint8_t kNibbleColorCount =
    sizeof(kNibbleColors) / sizeof(kNibbleColors[0]);

// A slow-filling green sweep, one full cycle every kChargingSweepMs -- distinct
// from every other animation's pace so charging is never mistaken for a game
// or a menu having been left running.
constexpr uint32_t kChargingSweepMs = 2600;

}  // namespace

void Engine::begin() {
  // Release the LED data pin's pad hold before anything tries to drive it.
  // enterDeepSleep() latches that pin low so the strip cannot pick up noise
  // while asleep, and the latch *survives the wake reset* -- without this the
  // strip would stay dark forever after the console's first sleep, since the
  // pad ignores the RMT peripheral while held. Harmless on a cold boot, where
  // there is no hold to release.
  gpio_hold_dis(static_cast<gpio_num_t>(board::kPinLedData));
  gpio_deep_sleep_hold_dis();

  // Hand the wake pins back to the digital IO subsystem. enterDeepSleep()
  // switches them to RTC function to hold their pullups through the sleep, and
  // that pad-mux change also survives the wake reset -- leaving Input::begin()'s
  // pinMode() below configuring a pad the RTC subsystem still owns.
  const uint8_t wake_pins[] = {board::kPinButtonA, board::kPinButtonB,
                               board::kPinStickSw};
  for (uint8_t pin : wake_pins) {
    rtc_gpio_deinit(static_cast<gpio_num_t>(pin));
  }

  display_.begin();
  input_.begin();
  storage_.begin();
  // Absent on a board with no fuel gauge (board::kHasBatteryMonitor); see
  // power.h. Nothing downstream needs to check this return value -- every
  // Power getter already answers as if nothing changed when unavailable.
  power_.begin();

  // A stored brightness of 0 means "never set", so fall back to the board cap.
  if (storage_.brightness() > 0) {
    display_.setBrightness(storage_.brightness());
  }

  last_frame_us_ = micros();
  scene_started_ms_ = millis();
  fps_window_start_ms_ = millis();
  last_activity_ms_ = millis();
}

void Engine::exitToLauncher() {
  if (launcher_ == nullptr) return;

  // File the score before leaving. Doing it here rather than in each game means
  // no cartridge can forget to, or cheat by reporting a score it never scored.
  if (scene_ != nullptr && current_game_ >= 0 &&
      current_game_ < static_cast<int8_t>(gameList().count())) {
    storage_.submitScore(gameList().at(current_game_).id, scene_->score());
    storage_.setLastGame(static_cast<uint8_t>(current_game_));
  }

  // Returning to the launcher is exactly the moment a flash write is invisible:
  // the frame is about to be thrown away anyway.
  storage_.commit();

  exit_gesture_progress_ = 0.0f;
  paused_ = false;
  exit_armed_ = false;
  current_game_ = -1;
  setScene(launcher_);
}

void Engine::setScene(Scene* scene) {
  // Deferred until the frame boundary so a scene can safely switch away from
  // itself during its own update().
  pending_scene_ = scene;
}

// While paused the game is frozen and dimmed, with a slow amber breathing pulse
// so the console never looks crashed. Holding B fills the line from the
// player's end as a progress bar toward returning to the launcher.
void Engine::renderPauseOverlay() {
  display_.fade(0.55f);

  if (exit_armed_ && exit_gesture_progress_ > 0.0f) {
    display_.span(0.0f, exit_gesture_progress_, colors::kAmber, 0.85f);
    return;
  }

  const float level = 0.35f + 0.35f * pulse(millis() / 1000.0f, 3.0f);
  display_.rawPixel(0, colors::kAmber.scaled(level));
  display_.rawPixel(display_.pixelCount() - 1, colors::kAmber.scaled(level));
}

bool Engine::isIdleActivity() const {
  // held(), not pressed(): a control being physically down counts as activity
  // for as long as it stays down, not just on the edge -- otherwise a game
  // left running with the stick pushed to one side would still idle out from
  // under the player.
  if (input_.held(Button::kA) || input_.held(Button::kB) ||
      input_.held(Button::kStick)) {
    return true;
  }
  // A small deadzone above whatever Input already applies: resting noise on a
  // cheap stick must not look like the player is still playing.
  constexpr float kIdleStickThreshold = 0.05f;
  return fabsf(input_.stickX()) > kIdleStickThreshold ||
         fabsf(input_.stickY()) > kIdleStickThreshold;
}

void Engine::updatePower(uint32_t now_ms) {
  power_.update(now_ms);

  if (isIdleActivity()) last_activity_ms_ = now_ms;

  // A critical battery pre-empts everything else the instant it is seen,
  // including a game mid-play and the pause/exit gesture below -- there is no
  // gesture worth letting the user finish when the goal is to get dirty
  // writes onto flash before the hardware protection circuit cuts power out
  // from under them.
  if (!shutting_down_ && power_.available() &&
      power_.level() == PowerLevel::kCritical) {
    beginCriticalShutdown();
  }
}

// A slow filling green sweep along the tube, distinct in both colour and pace
// from every other animation here, so charging never reads as a game or a
// menu accidentally left on screen.
void Engine::renderChargingAnimation() {
  display_.clear();
  const float phase =
      fmodf(static_cast<float>(millis() % kChargingSweepMs) /
                static_cast<float>(kChargingSweepMs),
            1.0f);
  const float fill = power_.percent() >= 99.0f ? 1.0f : phase;
  display_.span(0.0f, fill, colors::kGreen, 0.5f);
  // A bright leading edge marks the front of the sweep, so it reads as fill
  // progress rather than a static bar -- silent unless still filling.
  if (fill < 1.0f) display_.point(fill, colors::kGreen, 1.0f);
}

// Subtle and non-disruptive on purpose: a single pulsing pixel at one end,
// layered over whatever the scene already drew, so a low battery is visible
// during play without competing with it for attention. Only ever drawn for
// kLow -- kCritical takes over the whole display via the shutdown sweep
// instead, so this never has to fight that animation for the same pixels.
void Engine::renderLowBatteryOverlay() {
  if (!power_.available() || power_.level() != PowerLevel::kLow) return;
  const float level = 0.3f + 0.3f * pulse(millis() / 1000.0f, 1.5f);
  display_.rawPixel(display_.pixelCount() - 1, colors::kRed.scaled(level));
}

void Engine::beginCriticalShutdown() {
  shutting_down_ = true;
  shutdown_started_ms_ = millis();

  Serial.print("[power] CRITICAL at ");
  Serial.print(power_.percent(), 1);
  Serial.print("%  ");
  Serial.print(power_.voltage(), 2);
  Serial.println("V -- flushing and shutting down");

  // Flush now, before a single frame of the shutdown animation plays. This is
  // the entire point of detecting "critical" ahead of the hardware protection
  // circuit's own cutoff: get the write onto flash while there is still
  // guaranteed power to finish it.
  if (scene_ != nullptr && current_game_ >= 0 &&
      current_game_ < static_cast<int8_t>(gameList().count())) {
    storage_.submitScore(gameList().at(current_game_).id, scene_->score());
    storage_.setLastGame(static_cast<uint8_t>(current_game_));
  }
  storage_.commit();
}

// A red sweep closing in from both ends, unmistakably different from a pause
// or an exit gesture, so a battery-triggered shutdown never reads as the
// console having crashed or the player having done something wrong.
void Engine::renderCriticalShutdown(uint32_t elapsed_ms) {
  const float progress =
      elapsed_ms >= kCriticalShutdownMs
          ? 1.0f
          : static_cast<float>(elapsed_ms) /
                static_cast<float>(kCriticalShutdownMs);
  display_.clear();
  display_.span(0.0f, progress * 0.5f, colors::kRed, 1.0f);
  display_.span(1.0f - progress * 0.5f, 1.0f, colors::kRed, 1.0f);
}

void Engine::enterDeepSleep() {
  // Announced before the port goes away with the device. A console that sleeps
  // silently is indistinguishable from one that has crashed or browned out --
  // which, given this path is also reached by a critical battery, is exactly
  // the wrong thing to leave ambiguous in a log.
  Serial.print("[power] entering deep sleep");
  if (power_.available()) {
    Serial.print(" at ");
    Serial.print(power_.percent(), 1);
    Serial.print("%");
  }
  Serial.println(" -- press A, B or the stick to wake");
  Serial.flush();

  // Idempotent with beginCriticalShutdown()'s flush: entering deep sleep from
  // the idle path (as opposed to a critical battery) never called it, and a
  // second commit() when nothing is dirty is a no-op -- see Storage::commit().
  storage_.commit();
  display_.clear();
  display_.present();

  // Pin the LED data line low for the duration of the sleep. Deep sleep powers
  // down the digital IO subsystem, so this pin would otherwise float, and a
  // floating data line next to a NeoPixel strip is an antenna: the strip
  // latches whatever noise it decodes and sits there lit until something
  // drives it again. That is the half-strip-of-bright-white seen on wake --
  // the clear() above is genuinely presented, then undone by the float.
  pinMode(board::kPinLedData, OUTPUT);
  digitalWrite(board::kPinLedData, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(board::kPinLedData));
  gpio_deep_sleep_hold_en();

  // Every wake button lands within GPIO 0-21 on the S3 on both boards this
  // firmware targets, which is exactly the range ext1 wakeup supports; no
  // board-specific guard is needed here the way board_config.h needs one for
  // kHasBatteryMonitor.
  const uint64_t wake_mask = (1ULL << board::kPinButtonA) |
                             (1ULL << board::kPinButtonB) |
                             (1ULL << board::kPinStickSw);

  // Re-establish the pullups through the RTC subsystem, which is the part that
  // stays powered while asleep. Input::begin()'s INPUT_PULLUP configures the
  // *digital* IO pullup, and that is lost the moment deep sleep powers that
  // domain down -- leaving the wake pins floating, drifting low, and tripping
  // ANY_LOW within moments of sleeping.
  //
  // The symptom was a console that looked like it woke itself every couple of
  // minutes: sleep, spurious wake, full reset, ~1.3 s of boot, launcher. The
  // pullups have to be asserted here, not in Input::begin(), because they only
  // survive if they are set on the RTC side before the sleep begins.
  //
  // rtc_gpio_init() is what makes the rest of this take effect: until the pad
  // is switched to the RTC mux it is still owned by the digital IO subsystem,
  // and the RTC pullup setting below applies to a pad that is not listening.
  // Setting the pullup without it is a silent no-op -- which is exactly how
  // the first attempt at this fix still woke on GPIO10 (mask 0x400).
  const uint8_t wake_pins[] = {board::kPinButtonA, board::kPinButtonB,
                               board::kPinStickSw};
  for (uint8_t pin : wake_pins) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    rtc_gpio_init(gpio);
    rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(gpio);
    rtc_gpio_pullup_en(gpio);
  }

  // And keep the domain that drives those pullups powered. Left on AUTO the
  // chip is free to power RTC_PERIPH down, which switches the pullups off
  // partway into the sleep and reintroduces the float this whole block exists
  // to prevent -- intermittently, and only once already asleep, which is the
  // worst way for it to fail. The domain costs a few microamps next to the
  // milliamps the strip draws awake.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // All three are pulled up (above), so a press pulls the pin low -- wake on
  // any of them going low, not high.
  esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

void Engine::resetDiagnostics() {
  worst_frame_us_ = 0;
  frames_this_second_ = 0;
  fps_window_start_ms_ = millis();
}

void Engine::tick() {
  const uint32_t now_us = micros();

  // Unsigned subtraction handles the ~71 minute micros() rollover correctly.
  if (now_us - last_frame_us_ < kFrameIntervalUs) {
    // Not a frame. Normally this is dead time, but a scene that has opted in
    // gets serviced here -- see Engine::setIdleServiced(). This is what keeps
    // the WiFi stack responsive without giving it a whole frame's budget.
    if (idle_serviced_ && scene_ != nullptr) scene_->idle(*this);
    return;
  }
  last_frame_us_ = now_us;

  if (pending_scene_ != nullptr) {
    if (scene_ != nullptr) scene_->exit(*this);
    scene_ = pending_scene_;
    pending_scene_ = nullptr;
    scene_started_ms_ = millis();
    paused_ = false;
    exit_armed_ = false;
    exit_gesture_progress_ = 0.0f;
    // Opt-in, per scene. Cleared before enter() so a scene that wants idle
    // servicing must ask for it, and one that does not cannot inherit it from
    // whatever ran previously.
    idle_serviced_ = false;
    if (scene_ != nullptr) scene_->enter(*this);
    resetDiagnostics();
  }

  if (scene_ == nullptr) return;

  const uint32_t frame_start_us = micros();
  const uint32_t now_ms = millis();

  input_.update(now_ms);
  updatePower(now_ms);

  // A critical battery pre-empts absolutely everything below -- no pause
  // gesture, no scene update, nothing -- until the shutdown sweep finishes and
  // the device sleeps. See beginCriticalShutdown()'s comment for why the
  // flush already happened before this frame ever runs.
  if (shutting_down_) {
    // The battery climbing back out of critical mid-sweep means the reading
    // that started this was not to be trusted -- a real battery does not
    // recover on its own in two seconds. Sleeping anyway would mean acting on
    // information the console has already superseded, which is exactly what
    // put a fully-charged device to sleep one second into a boot. The flush
    // that already happened is harmless to keep.
    if (power_.available() && power_.level() != PowerLevel::kCritical) {
      Serial.print("[power] battery back to ");
      Serial.print(power_.percent(), 1);
      Serial.println("% -- shutdown aborted, resuming");
      shutting_down_ = false;
      last_activity_ms_ = now_ms;
    } else {
      const uint32_t elapsed = now_ms - shutdown_started_ms_;
      if (elapsed >= kCriticalShutdownMs) {
        enterDeepSleep();  // noreturn: the chip resets on wake
      }
      renderCriticalShutdown(elapsed);
      display_.present();
      return;
    }
  }

  // Idle handling comes before the pause/exit gesture too, so a console left
  // untouched in a paused game still sleeps rather than sitting frozen and
  // lit forever. Nothing here touches scene_ or its state: falling out of
  // this branch (any button or stick activity resets last_activity_ms_ at the
  // top of updatePower(), which runs before this check every frame) resumes
  // exactly where the scene left off, since it was never ticked while idle.
  {
    const uint32_t idle_ms = now_ms - last_activity_ms_;
    const bool charging = power_.available() && power_.charging();

    // Idle *and* charging: stay awake and show the sweep rather than going
    // dark, since sleeping saves nothing while USB is doing the powering
    // anyway (see power_policy.h).
    //
    // This has to be its own check rather than a branch inside the one below:
    // shouldEnterIdleSleep() deliberately returns false whenever charging, so
    // testing `charging` *after* it passed would be unreachable by
    // construction -- which is exactly the bug that kept this animation from
    // ever appearing.
    if (charging && idle_ms >= kIdleSleepMs) {
      renderChargingAnimation();
      display_.present();
      return;
    }

    if (shouldEnterIdleSleep(idle_ms, charging)) {
      const uint32_t fade_elapsed = idle_ms - kIdleSleepMs;
      if (fade_elapsed >= kIdleFadeMs) {
        enterDeepSleep();  // noreturn: the chip resets on wake
      }
      // Whatever was last drawn stays in the framebuffer and simply dims in
      // place -- see Display::fade()'s header comment for why repeated calls
      // reach true black rather than asymptoting just above it.
      display_.fade(0.08f);
      display_.present();
      return;
    }
  }

  // Pause and exit are handled by the engine, before the scene runs, so every
  // game behaves identically and none can swallow the gesture.
  //
  // Utility scenes (Network, Benchmark) are excluded: they need the nav button
  // for their own menus, and intercepting it here made them unusable -- the
  // press became a pause, and the paused branch returns before the scene ever
  // updates. They get a direct hold-B exit instead, with no pause step, since
  // they have no game state worth freezing.
  const bool is_game =
      current_game_ >= 0 &&
      current_game_ < static_cast<int8_t>(gameList().count()) &&
      gameList().at(current_game_).is_game;

  if (current_game_ >= 0 && launcher_ != nullptr && !is_game) {
    // Armed the same way as the paused gesture, so a B press carried in from
    // the launcher cannot instantly exit the scene it just opened.
    if (!exit_armed_) {
      if (!input_.held(Button::kB)) exit_armed_ = true;
      exit_gesture_progress_ = 0.0f;
    } else {
      const uint32_t held = input_.holdDuration(Button::kB);
      exit_gesture_progress_ =
          held == 0
              ? 0.0f
              : (held >= kExitHoldMs ? 1.0f
                                     : static_cast<float>(held) / kExitHoldMs);
      if (held >= kExitHoldMs) {
        exitToLauncher();
        return;
      }
    }
  }

  if (current_game_ >= 0 && launcher_ != nullptr && is_game) {
    // Exit is deliberately only available *while paused*. Games legitimately
    // use long holds during play -- Wormfight charges on B for up to 1.1 s --
    // so a bare hold-to-exit would fight the game's own controls. Requiring the
    // pause first makes the two unambiguous.
    if (input_.pressed(Input::kNavButton)) {
      paused_ = !paused_;
      exit_gesture_progress_ = 0.0f;
      exit_armed_ = false;
    }

    if (paused_) {
      // The exit gesture must be *armed* by seeing B released after the pause
      // began. Without this, holdDuration() would report time accrued before
      // the pause: pausing while mid-charge in Wormfight (B already held for
      // ~1.1 s) would trip the 1.2 s exit on the very first paused frame and
      // dump the player to the launcher without any gesture at all.
      if (!exit_armed_) {
        if (!input_.held(Button::kB)) exit_armed_ = true;
        exit_gesture_progress_ = 0.0f;
      } else {
        const uint32_t held = input_.holdDuration(Button::kB);
        exit_gesture_progress_ =
            held == 0 ? 0.0f
                      : (held >= kExitHoldMs
                             ? 1.0f
                             : static_cast<float>(held) / kExitHoldMs);

        if (held >= kExitHoldMs) {
          exitToLauncher();
          return;
        }
      }

      // Frozen: render the paused game so the player can see what they are
      // returning to, but do not advance it.
      scene_->render(*this);
      renderPauseOverlay();
      renderLowBatteryOverlay();
      display_.present();
      return;
    }
  }

  // A fixed timestep is used rather than measured elapsed time: game logic then
  // behaves identically whether or not a frame ran long, which matters once
  // scripted cartridges with variable cost are running.
  scene_->update(*this, kFixedDt);
  scene_->render(*this);

  // Utility scenes have no pause overlay to carry the exit gesture, so draw it
  // over whatever they rendered. Without visible feedback, holding B looks like
  // the console has simply stopped responding.
  if (!is_game && exit_gesture_progress_ > 0.0f) {
    display_.span(0.0f, exit_gesture_progress_, colors::kAmber, 0.8f);
  }

  renderLowBatteryOverlay();
  display_.present();

  const uint32_t frame_us = micros() - frame_start_us;
  if (frame_us > worst_frame_us_) worst_frame_us_ = frame_us;

  frames_this_second_++;
  if (now_ms - fps_window_start_ms_ >= 1000) {
    fps_ = frames_this_second_ * 1000.0f / (now_ms - fps_window_start_ms_);
    frames_this_second_ = 0;
    fps_window_start_ms_ = now_ms;
  }
}

namespace {

// Deliberately noinline: the Xtensa GCC shipped with the ESP32 platform hits an
// internal compiler error ("insn does not satisfy its constraints" during
// postreload, trying to load a float literal straight into an FP register) when
// this is inlined into the loop in renderScore(). Keeping the float maths in
// one non-inlined function sidesteps it and costs nothing at this call rate.
//
// The bit currently arriving fades up over its slot, so the reveal reads as
// bits landing one by one rather than simply appearing; settled bits are full
// bright.
float __attribute__((noinline)) bitIntensity(uint16_t bit, uint32_t revealed,
                                             uint32_t elapsed_ms,
                                             uint32_t per_bit_ms) {
  if (bit != revealed || per_bit_ms == 0) return 1.0f;
  const float progress = static_cast<float>(elapsed_ms % per_bit_ms) /
                         static_cast<float>(per_bit_ms);
  return 0.35f + 0.65f * progress;
}

}  // namespace

void Engine::renderScore(uint32_t score, uint32_t elapsed_ms, bool instant) {
  const uint16_t pixels = display_.pixelCount();

  // Only render as many bits as the score needs, so a small score does not look
  // like a mostly-empty display.
  uint8_t significant_bits = 1;
  for (uint8_t bit = 0; bit < 32; bit++) {
    if (score & (1UL << bit)) significant_bits = bit + 1;
  }

  // Anchor the readout at pixel 0 rather than centring it. A fixed origin means
  // a given bit always appears at the same place, so values can be read by
  // position; centring would shift the whole display as the score gains bits.
  const uint16_t bits_to_show =
      significant_bits > pixels ? pixels : significant_bits;

  // A score of zero has no bits to light, so mark it with a single dim pixel at
  // the origin -- otherwise the readout is indistinguishable from a crash.
  if (score == 0) {
    display_.rawPixel(0, Color(40, 40, 40));
    return;
  }

  if (instant) {
    // A glance at a score that already happened -- e.g. the launcher peeking
    // at a highscore -- should read the whole value at once, not perform the
    // reveal flourish that belongs to a score just earned.
    for (uint16_t bit = 0; bit < bits_to_show; bit++) {
      if ((score & (1UL << bit)) != 0) drawScoreBit(bit, 1.0f);
    }
    return;
  }

  // Reveal one bit at a time, then hold. Reading as a deliberate flourish
  // rather than a limitation is most of the point.
  const uint32_t per_bit_ms =
      bits_to_show > 0 ? kScoreRevealMs / bits_to_show : kScoreRevealMs;
  const uint32_t revealed =
      per_bit_ms > 0 ? (elapsed_ms / per_bit_ms) : bits_to_show;

  for (uint16_t bit = 0; bit < bits_to_show; bit++) {
    if (bit > revealed) break;

    const bool set = (score & (1UL << bit)) != 0;
    if (!set) continue;

    drawScoreBit(bit, bitIntensity(bit, revealed, elapsed_ms, per_bit_ms));
  }
}

void Engine::drawScoreBit(uint16_t bit, float intensity) {
  const Color color = kNibbleColors[(bit / 4) % kNibbleColorCount];
  display_.rawPixel(bit, color.scaled(intensity));
}

}  // namespace beamboy
