#pragma once

// Beam Boy — input abstraction.
//
// Two kinds of control, deliberately:
//
//   * Buttons are digital and edge-detected, so games can distinguish a tap
//     from a hold without tracking timing themselves.
//   * The joystick is analog (both X and Y axes), giving continuous velocity/
//     position control.
//
// Discrete navigation steps for menus (navDelta) are synthesized from the
// joystick's X axis.

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
  // curve shaped. Positive is toward the far end of the strip (right).
  float stickX() const { return stick_x_; }

  // Vertical axis, -1.0 to +1.0, with the deadzone applied and the response
  // curve shaped. Positive is forward / up.
  float stickY() const { return stick_y_; }

  // Raw axis values before shaping, for calibration and diagnostics.
  float rawStickX() const { return raw_stick_x_; }
  float rawStickY() const { return raw_stick_y_; }

  // Record the stick's resting position as centre for both X and Y. Cheap
  // sticks rarely rest at exactly mid-scale, so this is called at boot with
  // the stick untouched.
  void calibrateCenter();

  // Invert axes if the stick is mounted the other way round in the case.
  void setStickInverted(bool inverted_x, bool inverted_y = false) {
    stick_x_inverted_ = inverted_x;
    stick_y_inverted_ = inverted_y;
  }
  void setStickXInverted(bool inverted) { stick_x_inverted_ = inverted; }
  void setStickYInverted(bool inverted) { stick_y_inverted_ = inverted; }

  // --- Navigation ----------------------------------------------------------
  //
  // Menus want *discrete steps*, not a continuous axis: one detent, one item.
  // This is that abstraction, synthesized from horizontal joystick movement.
  //
  // Push the stick past a threshold and it emits one step immediately, then
  // auto-repeats while held, the way a held arrow key does.
  //
  // Steps since the last frame. Positive is toward the far end of the strip.
  // Usually -1, 0 or +1.
  int8_t navDelta() const { return nav_delta_; }

  // The button that confirms a menu selection (the stick's push switch).
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
  float stick_center_x_ = 0.5f;
  bool stick_x_inverted_ = false;

  float stick_y_ = 0.0f;
  float raw_stick_y_ = 0.0f;
  float stick_center_y_ = 0.5f;
  bool stick_y_inverted_ = false;

  // Synthesized detent stepping. nav_hold_ms_ tracks how long the stick has
  // been past the threshold, which drives the auto-repeat.
  int8_t nav_delta_ = 0;
  int8_t nav_direction_ = 0;
  uint32_t nav_hold_ms_ = 0;
  uint32_t nav_last_step_ms_ = 0;
};

}  // namespace beamboy
