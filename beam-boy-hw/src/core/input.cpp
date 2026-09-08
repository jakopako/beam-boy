#include "input.h"

namespace beamboy {
namespace {

// Cheap analog sticks drift and jitter around centre. Anything inside the
// deadzone reads as zero so the player does not creep while the stick is idle.
constexpr float kDeadzone = 0.12f;

// Exponent applied to the axis after the deadzone. Values above 1 make small
// deflections gentler while leaving full deflection at full speed, which makes
// fine positioning much easier without sacrificing top speed.
constexpr float kResponseCurve = 1.6f;

// Buttons are sampled once per frame (~16 ms), which is already slower than
// most contact bounce, but a short lockout removes the rest.
constexpr uint32_t kDebounceMs = 25;

// --- Synthesized navigation ------------------------------------------------
// Deflection past which the stick counts as "turned one detent". Set well above
// the deadzone so a resting hand never scrolls a menu.
constexpr float kNavThreshold = 0.55f;

// Below this, the stick must return before it can step again. The gap between
// the two is hysteresis: without it, a stick hovering near the threshold would
// emit a stream of steps.
constexpr float kNavRelease = 0.30f;

// Auto-repeat while held, like a held arrow key: one step, a pause to let go,
// then a steady stream.
constexpr uint32_t kNavRepeatDelayMs = 400;
constexpr uint32_t kNavRepeatRateMs = 140;

uint8_t indexOf(Button button) { return static_cast<uint8_t>(button); }

}  // namespace

void Input::begin() {
  pinMode(board::kPinButtonA, INPUT_PULLUP);
  pinMode(board::kPinButtonB, INPUT_PULLUP);
  pinMode(board::kPinStickSw, INPUT_PULLUP);

  calibrateCenter();
}

void Input::calibrateCenter() {
  // Average several samples: a single ADC reading is noisy enough to bias the
  // centre by more than the deadzone.
  uint32_t total = 0;
  constexpr uint8_t kSamples = 16;
  for (uint8_t i = 0; i < kSamples; i++) {
    total += analogRead(board::kPinStickX);
    delay(2);
  }
  stick_center_ = static_cast<float>(total) /
                  (kSamples * static_cast<float>(board::kAdcMax));
}

void Input::update(uint32_t now_ms) {
  now_ms_ = now_ms;

  const uint8_t pins[] = {board::kPinButtonA, board::kPinButtonB,
                          board::kPinStickSw};

  for (uint8_t i = 0; i < static_cast<uint8_t>(Button::kCount); i++) {
    ButtonState& state = buttons_[i];
    state.previous_down = state.down;

    // Buttons pull to ground, so LOW means pressed.
    const bool raw_down = digitalRead(pins[i]) == LOW;

    // The debounce is a lockout, but a *latching* one. Naively re-reading the
    // pin after the lockout expires loses any transition that both started and
    // ended inside the window -- a fast double-tap would then register as one
    // press, or none. Remembering the last raw level seen means the transition
    // is merely delayed, never dropped.
    state.pending_down = raw_down;

    if (state.pending_down != state.down) {
      const bool settled =
          state.changed_at == 0 || (now_ms - state.changed_at) >= kDebounceMs;
      if (settled) {
        state.down = state.pending_down;
        state.changed_at = now_ms;
        if (state.down) state.pressed_at = now_ms;
      }
    }
  }

  // --- Joystick ------------------------------------------------------------
  const float normalised =
      static_cast<float>(analogRead(board::kPinStickX)) / board::kAdcMax;
  raw_stick_x_ = normalised;

  // Express deflection relative to the calibrated centre. The two halves are
  // scaled independently because the centre is rarely at exactly mid-scale, and
  // treating them as one range would make one direction reach full speed early.
  float offset;
  if (normalised >= stick_center_) {
    const float range = 1.0f - stick_center_;
    offset = range > 0.0f ? (normalised - stick_center_) / range : 0.0f;
  } else {
    const float range = stick_center_;
    offset = range > 0.0f ? (normalised - stick_center_) / range : 0.0f;
  }

  const float magnitude = fabsf(offset);
  if (magnitude < kDeadzone) {
    stick_x_ = 0.0f;
  } else {
    // Rescale so the axis still reaches 1.0 at full deflection despite the
    // deadzone consuming part of the travel.
    const float scaled = (magnitude - kDeadzone) / (1.0f - kDeadzone);
    const float shaped = powf(scaled, kResponseCurve);
    stick_x_ = offset < 0.0f ? -shaped : shaped;
  }

  if (stick_inverted_) stick_x_ = -stick_x_;

  updateNav();
}

// Turns the continuous axis into discrete steps. When the rotary encoder
// arrives this is the *only* function that changes: it will read quadrature
// pulses and write nav_delta_ directly. Everything downstream is unaffected,
// which is the entire point of routing menus through navDelta().
void Input::updateNav() {
  nav_delta_ = 0;

  const float magnitude = fabsf(stick_x_);
  const int8_t direction = stick_x_ > 0.0f ? 1 : -1;

  // Released, or pushed the other way: reset and allow an immediate step.
  if (magnitude < kNavRelease ||
      (nav_direction_ != 0 && direction != nav_direction_)) {
    nav_direction_ = 0;
    nav_hold_ms_ = 0;
    return;
  }

  if (magnitude < kNavThreshold) return;

  if (nav_direction_ == 0) {
    // First crossing: step immediately, so a deliberate flick always responds
    // on the same frame rather than after the repeat delay.
    nav_direction_ = direction;
    nav_hold_ms_ = now_ms_;
    nav_last_step_ms_ = now_ms_;
    nav_delta_ = direction;
    return;
  }

  if (now_ms_ - nav_hold_ms_ < kNavRepeatDelayMs) return;
  if (now_ms_ - nav_last_step_ms_ < kNavRepeatRateMs) return;

  nav_last_step_ms_ = now_ms_;
  nav_delta_ = nav_direction_;
}

bool Input::pressed(Button button) const {
  const ButtonState& state = buttons_[indexOf(button)];
  return state.down && !state.previous_down;
}

bool Input::released(Button button) const {
  const ButtonState& state = buttons_[indexOf(button)];
  return !state.down && state.previous_down;
}

bool Input::held(Button button) const { return buttons_[indexOf(button)].down; }

uint32_t Input::holdDuration(Button button) const {
  const ButtonState& state = buttons_[indexOf(button)];
  if (!state.down) return 0;
  return now_ms_ - state.pressed_at;
}

bool Input::heldFor(Button button, uint32_t duration_ms) const {
  return holdDuration(button) >= duration_ms;
}

}  // namespace beamboy
