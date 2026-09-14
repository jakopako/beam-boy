// Tests for the launcher's button-gesture arbitration.
//
// Three gestures share two buttons -- tap A to launch, hold B to delete, hold
// A+B for the battery gauge -- and every bug found in that overlap so far has
// been an ordering bug measured in milliseconds between two thumbs. Two of
// them destroyed a cartridge and its highscore. None of them reproduce
// reliably by hand. So the arbitration is pure logic (no Arduino, no Input)
// precisely so these sequences can be replayed frame by frame here.

#include <unity.h>

#include "scenes/launcher_gestures.h"

using namespace beamboy;

namespace {

// A tiny frame-driver: tracks hold durations the way Input does, so tests can
// describe a gesture in terms of which buttons are down over time.
class Rig {
 public:
  Rig() { gestures_.reset(); }

  // Arms the arbiter the way a normal launcher entry does -- one frame with
  // nothing pressed.
  void armed() { frame(false, false, 0); }

  GestureResult frame(bool a_down, bool b_down, uint32_t dt_ms) {
    ButtonSnapshot s;
    s.a_released = a_was_down_ && !a_down;

    a_hold_ms_ = a_down ? (a_was_down_ ? a_hold_ms_ + dt_ms : 0) : 0;
    b_hold_ms_ = b_down ? (b_was_down_ ? b_hold_ms_ + dt_ms : 0) : 0;

    s.a_down = a_down;
    s.b_down = b_down;
    s.a_hold_ms = a_hold_ms_;
    s.b_hold_ms = b_hold_ms_;

    a_was_down_ = a_down;
    b_was_down_ = b_down;
    return gestures_.update(s);
  }

  // Holds the given buttons for `total_ms` in 16 ms frames, returning the last
  // frame's result.
  GestureResult hold(bool a_down, bool b_down, uint32_t total_ms) {
    GestureResult out;
    for (uint32_t t = 0; t < total_ms; t += 16) out = frame(a_down, b_down, 16);
    return out;
  }

  void reset() { gestures_.reset(); }

 private:
  LauncherGestures gestures_;
  bool a_was_down_ = false;
  bool b_was_down_ = false;
  uint32_t a_hold_ms_ = 0;
  uint32_t b_hold_ms_ = 0;
};

// --- launching -------------------------------------------------------------

// The ordinary case: tap A, get a launch on release.
void test_a_plain_tap_of_a_launches_on_release() {
  Rig rig;
  rig.armed();
  TEST_ASSERT_FALSE(rig.frame(true, false, 16).launch);
  TEST_ASSERT_TRUE(rig.frame(false, false, 16).launch);
}

// A press alone must not launch -- that is the edge the A+B combo needs back.
void test_pressing_a_does_not_launch_until_released() {
  Rig rig;
  rig.armed();
  TEST_ASSERT_FALSE(rig.hold(true, false, 2000).launch);
}

// The reported bug: reaching for the gauge and catching A a frame or two
// before B used to launch a game instead.
void test_a_pressed_slightly_before_b_never_launches() {
  Rig rig;
  rig.armed();
  rig.frame(true, false, 16);  // A lands first
  rig.frame(true, true, 16);   // B follows a frame later
  rig.hold(true, true, 1000);  // gauge shown for a while
  TEST_ASSERT_FALSE(rig.frame(false, true, 16).launch);   // A up first
  TEST_ASSERT_FALSE(rig.frame(false, false, 16).launch);  // then B
}

// The mirror case: B first, then A. Also a gauge, also never a launch.
void test_b_pressed_before_a_never_launches() {
  Rig rig;
  rig.armed();
  rig.frame(false, true, 16);
  rig.frame(true, true, 16);
  rig.hold(true, true, 1000);
  TEST_ASSERT_FALSE(rig.frame(true, false, 16).launch);
  TEST_ASSERT_FALSE(rig.frame(false, false, 16).launch);
}

// Releasing both on the same frame must still read as a combo, not a launch --
// the latch has to outlive the frame it is cleared on.
void test_releasing_both_together_does_not_launch() {
  Rig rig;
  rig.armed();
  rig.hold(true, true, 1000);
  TEST_ASSERT_FALSE(rig.frame(false, false, 16).launch);
}

// After a combo fully ends, the buttons work normally again.
void test_a_launches_normally_after_a_combo_ends() {
  Rig rig;
  rig.armed();
  rig.hold(true, true, 1000);
  rig.frame(false, false, 16);  // combo ends
  rig.frame(true, false, 16);
  TEST_ASSERT_TRUE(rig.frame(false, false, 16).launch);
}

// --- the battery gauge -----------------------------------------------------

void test_the_gauge_appears_only_after_the_combo_hold() {
  Rig rig;
  rig.armed();
  TEST_ASSERT_FALSE(rig.hold(true, true, kBatteryHoldMs - 100).show_battery);
  TEST_ASSERT_TRUE(rig.hold(true, true, 200).show_battery);
}

// The combo's age is measured from whichever button arrived *later*, so a
// long-held B does not shortcut the gauge's own threshold.
void test_a_long_held_b_does_not_shortcut_the_gauge_threshold() {
  Rig rig;
  rig.armed();
  rig.hold(false, true, 2000);  // B alone for a while first
  TEST_ASSERT_FALSE(rig.frame(true, true, 16).show_battery);
  TEST_ASSERT_FALSE(rig.hold(true, true, kBatteryHoldMs - 100).show_battery);
  TEST_ASSERT_TRUE(rig.hold(true, true, 200).show_battery);
}

// Letting go of either button drops the gauge immediately.
void test_the_gauge_disappears_as_soon_as_a_button_is_released() {
  Rig rig;
  rig.armed();
  TEST_ASSERT_TRUE(rig.hold(true, true, kBatteryHoldMs + 100).show_battery);
  TEST_ASSERT_FALSE(rig.frame(false, true, 16).show_battery);
}

// --- deleting --------------------------------------------------------------

void test_a_plain_b_hold_counts_toward_a_delete() {
  Rig rig;
  rig.armed();
  TEST_ASSERT_TRUE(rig.hold(false, true, kDeleteHoldMs + 100).delete_hold_ms >=
                   kDeleteHoldMs);
}

void test_a_short_b_hold_does_not_reach_the_delete_threshold() {
  Rig rig;
  rig.armed();
  TEST_ASSERT_TRUE(rig.hold(false, true, 500).delete_hold_ms < kDeleteHoldMs);
}

// A delete countdown already running is called off the moment A joins, rather
// than completing underneath the gauge.
void test_pressing_a_suppresses_a_delete_already_counting_down() {
  Rig rig;
  rig.armed();
  rig.hold(false, true, kDeleteHoldMs - 200);
  TEST_ASSERT_EQUAL_UINT32(0, rig.frame(true, true, 16).delete_hold_ms);
  TEST_ASSERT_EQUAL_UINT32(0, rig.hold(true, true, 1000).delete_hold_ms);
}

// The dangerous one: after several seconds on the gauge, B's hold duration is
// already well past the delete threshold, so letting go of A first must not
// hand a completed delete gesture to the launcher.
void test_releasing_a_after_the_gauge_does_not_delete() {
  Rig rig;
  rig.armed();
  rig.hold(true, true, kDeleteHoldMs + 1000);
  TEST_ASSERT_EQUAL_UINT32(0, rig.frame(false, true, 16).delete_hold_ms);
  TEST_ASSERT_EQUAL_UINT32(0, rig.hold(false, true, 1000).delete_hold_ms);
}

// ...and once B is finally released, a genuinely fresh hold works again.
void test_a_fresh_b_hold_after_the_gauge_deletes_normally() {
  Rig rig;
  rig.armed();
  rig.hold(true, true, kDeleteHoldMs + 1000);
  rig.frame(false, true, 16);
  rig.frame(false, false, 16);  // both up: the latch clears here
  TEST_ASSERT_TRUE(rig.hold(false, true, kDeleteHoldMs + 100).delete_hold_ms >=
                   kDeleteHoldMs);
}

// --- arming ----------------------------------------------------------------

// Entering the launcher mid-hold: the B press that exited a game (1.2 s) is
// still down and already past the 2.5 s delete threshold by the time the
// launcher sees it. It must not delete the cartridge the player just left.
void test_a_hold_carried_in_from_the_previous_scene_never_deletes() {
  Rig rig;
  rig.reset();  // as LauncherScene::enter() does
  TEST_ASSERT_EQUAL_UINT32(
      0, rig.hold(false, true, kDeleteHoldMs + 2000).delete_hold_ms);
}

// The same carried-in hold must not launch, either, when it is finally let go.
void test_a_hold_carried_in_from_the_previous_scene_never_launches() {
  Rig rig;
  rig.reset();
  rig.hold(true, false, 2000);
  TEST_ASSERT_FALSE(rig.frame(false, false, 16).launch);
}

// Once released, everything arms and behaves normally.
void test_gestures_work_again_after_the_carried_in_hold_is_released() {
  Rig rig;
  rig.reset();
  rig.hold(false, true, 3000);
  rig.frame(false, false, 16);  // arms here
  TEST_ASSERT_TRUE(rig.hold(false, true, kDeleteHoldMs + 100).delete_hold_ms >=
                   kDeleteHoldMs);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_plain_tap_of_a_launches_on_release);
  RUN_TEST(test_pressing_a_does_not_launch_until_released);
  RUN_TEST(test_a_pressed_slightly_before_b_never_launches);
  RUN_TEST(test_b_pressed_before_a_never_launches);
  RUN_TEST(test_releasing_both_together_does_not_launch);
  RUN_TEST(test_a_launches_normally_after_a_combo_ends);
  RUN_TEST(test_the_gauge_appears_only_after_the_combo_hold);
  RUN_TEST(test_a_long_held_b_does_not_shortcut_the_gauge_threshold);
  RUN_TEST(test_the_gauge_disappears_as_soon_as_a_button_is_released);
  RUN_TEST(test_a_plain_b_hold_counts_toward_a_delete);
  RUN_TEST(test_a_short_b_hold_does_not_reach_the_delete_threshold);
  RUN_TEST(test_pressing_a_suppresses_a_delete_already_counting_down);
  RUN_TEST(test_releasing_a_after_the_gauge_does_not_delete);
  RUN_TEST(test_a_fresh_b_hold_after_the_gauge_deletes_normally);
  RUN_TEST(test_a_hold_carried_in_from_the_previous_scene_never_deletes);
  RUN_TEST(test_a_hold_carried_in_from_the_previous_scene_never_launches);
  RUN_TEST(test_gestures_work_again_after_the_carried_in_hold_is_released);
  return UNITY_END();
}
