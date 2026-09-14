#pragma once

#include <stdint.h>

// Launcher button-gesture arbitration, kept deliberately free of any Arduino
// or scene dependency so it can be tested on the host.
//
// This exists for the same reason power_policy.h and net_policy.h do, but the
// case for it is sharper: three separate gestures are built on just two
// buttons -- tap A to launch, hold B to delete, hold A+B to show the battery
// gauge -- and they overlap. Every bug found here so far has been an *ordering*
// bug rather than a threshold one:
//
//   * Pressing A a few milliseconds before B launched a game instead of
//     showing the gauge, because A's press edge had already fired.
//   * Letting go of A first after a few seconds on the gauge deleted the
//     selected cartridge, because B's hold duration was already past the
//     delete threshold.
//   * Holding B to exit a game (1.2 s) rolled straight on past the launcher's
//     delete threshold (2.5 s) and wiped the cartridge the player was only
//     trying to leave.
//
// None of those reproduce reliably by hand -- they depend on millisecond
// ordering between two thumbs -- and the last two destroy user data when they
// do. That is the whole argument for pulling the arbitration out of the scene
// and testing it directly.

namespace beamboy {

// How long A and B must be held *together* before the battery gauge replaces
// the list. Long enough that a stray A-then-B while launching can't trigger
// it; short enough that a deliberate peek doesn't feel sluggish.
constexpr uint32_t kBatteryHoldMs = 500;

// How long B alone must be held on an installed cartridge before it is
// deleted.
constexpr uint32_t kDeleteHoldMs = 2500;

static_assert(kBatteryHoldMs < kDeleteHoldMs,
              "the gauge must appear before the delete countdown could ever "
              "complete, or holding A+B would race a deletion");

// One frame's worth of the two face buttons, as the launcher sees them.
struct ButtonSnapshot {
  bool a_down = false;
  bool b_down = false;
  // True for exactly the frame A comes up, matching Input::released().
  bool a_released = false;
  // Milliseconds each button has been down, 0 when up (Input::holdDuration()).
  uint32_t a_hold_ms = 0;
  uint32_t b_hold_ms = 0;
};

struct GestureResult {
  // Launch the selected entry. Fires on A's *release*, never its press: at
  // press time it is not yet knowable whether B is about to join and make this
  // a gauge gesture instead.
  bool launch = false;

  // Show the battery gauge for this frame.
  bool show_battery = false;

  // Effective duration of a legitimate B-only delete hold, or 0 whenever the
  // gesture is suppressed.
  //
  // Returning the duration rather than a bare "delete now" boolean is what
  // keeps the countdown the player sees and the deletion that eventually fires
  // from ever disagreeing: both read this one number, so a suppressed hold
  // renders no warning *and* performs no delete, with no second condition to
  // keep in sync.
  uint32_t delete_hold_ms = 0;
};

class LauncherGestures {
 public:
  // Called when the launcher is entered. Leaves the arbiter disarmed, so no
  // gesture fires until both buttons have been seen up at least once.
  void reset() {
    combo_engaged_ = false;
    armed_ = false;
  }

  GestureResult update(const ButtonSnapshot& buttons) {
    GestureResult out;

    // Latches as soon as both are down and clears only once both are back up.
    // Outliving the combo itself is the point: it keeps the individual A and
    // B gestures suppressed through the messy part where one thumb has
    // lifted and the other has not.
    //
    // Set here but cleared at the bottom, so a decision below always sees the
    // latch if it applied at any point up to and including this frame.
    if (buttons.a_down && buttons.b_down) combo_engaged_ = true;

    // Deliberately reads armed_ *before* the bottom of this function can set
    // it. Arming on the same frame as a release would defeat the whole guard:
    // the release that ends a carried-in hold is exactly the frame that would
    // both arm the arbiter and fire A's launch off the back of it.
    out.launch = armed_ && buttons.a_released && !combo_engaged_;

    if (buttons.a_down && buttons.b_down) {
      // Whichever button was pressed later started the combo, so the combo's
      // age is the smaller of the two hold durations.
      const uint32_t combo_ms = buttons.a_hold_ms < buttons.b_hold_ms
                                     ? buttons.a_hold_ms
                                     : buttons.b_hold_ms;
      out.show_battery = combo_ms >= kBatteryHoldMs;
    }

    // combo_engaged_ covers the "A is also down right now" case on its own,
    // including the very first frame of a combo, since it was set above.
    if (armed_ && !combo_engaged_ && buttons.b_down) {
      out.delete_hold_ms = buttons.b_hold_ms;
    }

    // Both buttons up is the one unambiguous moment: nothing is carried over
    // from a previous scene, and no combo is half-released. Arm here, and
    // clear the combo latch here, after every decision above has read them.
    if (!buttons.a_down && !buttons.b_down) {
      armed_ = true;
      combo_engaged_ = false;
    }

    return out;
  }

 private:
  bool combo_engaged_ = false;
  bool armed_ = false;
};

}  // namespace beamboy
