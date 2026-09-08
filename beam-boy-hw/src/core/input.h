#pragma once

// Beam Boy — input abstraction.
//
// Two kinds of control, deliberately:
//
//   * Buttons are digital and edge-detected, so games can distinguish a tap
//     from a hold without tracking timing themselves.
//   * The joystick is analog, giving velocity control ("creep left" as distinct
//     from "dash left") that a stepped encoder cannot express.
//
// Only the joystick's X axis is read. On a 1D display Y has no natural
// meaning, so it is skipped.

#include <Arduino.h>

#include "board_config.h"

namespace beamboy {

enum class Button : uint8_t {
  kA = 0,
  kB,
  kStick,
  kCount,
};

class Input {
 public:
  void begin();

  // Sample the hardware. Called once per frame by the engine, before scenes
  // update, so every scene sees a consistent snapshot.
  void update(uint32_t now_ms);

  // --- Buttons -------------------------------------------------------------

  // True for exactly one frame, when the button goes down.
  bool pressed(Button button) const;

  // True for exactly one frame, when the button comes up.
  bool released(Button button) const;

  // True for as long as the button is down.
  bool held(Button button) const;

  // True once the button has been held for at least the given duration --
  // used for "hold B to exit" style gestures.
  bool heldFor(Button button, uint32_t duration_ms) const;

  // How long the button has been down, or 0 if it is up.
  uint32_t holdDuration(Button button) const;

  // --- Joystick ------------------------------------------------------------

  // Horizontal axis, -1.0 to +1.0, with the deadzone applied and the response
  // curve shaped. Positive is toward the far end of the strip.
  float stickX() const { return stick_x_; }

  // Raw axis value before shaping, for calibration and diagnostics.
  float rawStickX() const { return raw_stick_x_; }

  // Record the stick's resting position as centre. Cheap sticks rarely rest at
  // exactly mid-scale, so this is called at boot with the stick untouched.
  void calibrateCenter();

  // Invert the axis if the stick is mounted the other way round in the case.
  void setStickInverted(bool inverted) { stick_inverted_ = inverted; }

  // --- Navigation ----------------------------------------------------------
  //
  // Menus want *discrete steps*, not a continuous axis: one detent, one item.
  // This is that abstraction, and it exists so that menu code never talks to a
  // specific input device.
  //
  // Today the steps are synthesized from the joystick, because the rotary
  // encoder has not arrived yet. When it does, only Input::update() changes --
  // it will read real quadrature pulses and feed them into the same counter.
  // Nothing that consumes navDelta() needs to know which happened.
  //
  // Push the stick past a threshold and it emits one step immediately, then
  // auto-repeats while held, the way a held arrow key does. That is not a
  // perfect imitation of a detented wheel, but it is the same *interaction*:
  // discrete, countable, one item at a time.

  // Steps since the last frame. Positive is toward the far end of the strip.
  // Usually -1, 0 or +1; a fast encoder spin can yield more.
  int8_t navDelta() const { return nav_delta_; }

  // The button that confirms a menu selection. Currently the stick's push
  // switch; with the encoder fitted this becomes the encoder's push switch.
  static constexpr Button kNavButton = Button::kStick;

 private:
  void updateNav();

  struct ButtonState {
    bool down = false;
    bool previous_down = false;
    // Last raw level sampled, held until the debounce lockout lets it through.
    bool pending_down = false;
    // When the debounced level last changed, which drives the lockout.
    uint32_t changed_at = 0;
    // When the button last went *down*, which drives holdDuration().
    uint32_t pressed_at = 0;
  };

  ButtonState buttons_[static_cast<uint8_t>(Button::kCount)];
  uint32_t now_ms_ = 0;

  float stick_x_ = 0.0f;
  float raw_stick_x_ = 0.0f;
  float stick_center_ = 0.5f;
  bool stick_inverted_ = false;

  // Synthesized detent stepping. nav_hold_ms_ tracks how long the stick has
  // been past the threshold, which drives the auto-repeat.
  int8_t nav_delta_ = 0;
  int8_t nav_direction_ = 0;
  uint32_t nav_hold_ms_ = 0;
  uint32_t nav_last_step_ms_ = 0;
};

}  // namespace beamboy
