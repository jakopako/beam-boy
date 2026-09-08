#include "engine.h"

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

}  // namespace

void Engine::begin() {
  display_.begin();
  input_.begin();
  storage_.begin();

  // A stored brightness of 0 means "never set", so fall back to the board cap.
  if (storage_.brightness() > 0) {
    display_.setBrightness(storage_.brightness());
  }

  last_frame_us_ = micros();
  scene_started_ms_ = millis();
  fps_window_start_ms_ = millis();
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

void Engine::renderScore(uint32_t score, uint32_t elapsed_ms) {
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

  // Reveal one bit at a time, then hold. Reading as a deliberate flourish
  // rather than a limitation is most of the point.
  const uint32_t per_bit_ms =
      bits_to_show > 0 ? kScoreRevealMs / bits_to_show : kScoreRevealMs;
  const uint32_t revealed =
      per_bit_ms > 0 ? (elapsed_ms / per_bit_ms) : bits_to_show;

  // A score of zero has no bits to light, so mark it with a single dim pixel at
  // the origin -- otherwise the readout is indistinguishable from a crash.
  if (score == 0) {
    display_.rawPixel(0, Color(40, 40, 40));
    return;
  }

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
