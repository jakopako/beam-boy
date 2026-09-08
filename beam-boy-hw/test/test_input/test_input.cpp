// Beam Boy — host tests for the input layer.
//
// This is the best case for host testing in the whole codebase. Debounce and
// auto-repeat are pure timing logic over a pin level, and on hardware they can
// only be checked by pressing buttons and watching for something that *should
// not* happen -- a dropped double-tap, a menu that scrolls one item too far.
// Here the clock is a variable, so each of those cases is stated exactly.

#include <unity.h>

#include "core/input.h"

using namespace beamboy;

namespace {

void setClock(uint32_t ms) { beamboy_host::state().now_us = ms * 1000UL; }

void setButton(uint8_t pin, bool down) {
  // Buttons pull to ground, so pressed is LOW.
  beamboy_host::state().pin_digital[pin] = down ? LOW : HIGH;
}

void setStick(float normalised) {
  beamboy_host::state().pin_analog[board::kPinStickX] =
      static_cast<int>(normalised * board::kAdcMax);
}

// Advances the simulated clock and samples input, the way the engine does once
// per frame.
void tick(Input& input, uint32_t ms) {
  setClock(ms);
  input.update(ms);
}

Input freshInput() {
  beamboy_host::state().reset();
  setStick(0.5f);
  Input input;
  input.begin();
  return input;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_press_and_release_are_single_frame_events(void) {
  Input input = freshInput();
  tick(input, 100);
  TEST_ASSERT_FALSE(input.pressed(Button::kA));

  setButton(board::kPinButtonA, true);
  tick(input, 200);
  TEST_ASSERT_TRUE(input.pressed(Button::kA));
  TEST_ASSERT_TRUE(input.held(Button::kA));

  // Still down, but pressed() must not fire again.
  tick(input, 216);
  TEST_ASSERT_FALSE(input.pressed(Button::kA));
  TEST_ASSERT_TRUE(input.held(Button::kA));

  setButton(board::kPinButtonA, false);
  tick(input, 400);
  TEST_ASSERT_TRUE(input.released(Button::kA));
  TEST_ASSERT_FALSE(input.held(Button::kA));

  tick(input, 416);
  TEST_ASSERT_FALSE(input.released(Button::kA));
}

void test_fast_double_tap_is_not_swallowed_by_debounce(void) {
  // The bug this guards against: a naive debounce that re-reads the pin after
  // the lockout expires loses any transition that began and ended inside the
  // window, so a fast double-tap registers as one press or none. The latch in
  // Input::update() delays such a transition instead of dropping it.
  Input input = freshInput();
  tick(input, 100);

  setButton(board::kPinButtonA, true);
  tick(input, 110);
  TEST_ASSERT_TRUE(input.pressed(Button::kA));

  // Release well inside the 25 ms lockout.
  setButton(board::kPinButtonA, false);
  tick(input, 118);

  // Keep ticking; the release must eventually be delivered, not lost.
  bool saw_release = false;
  for (uint32_t t = 126; t <= 300 && !saw_release; t += 8) {
    tick(input, t);
    if (input.released(Button::kA)) saw_release = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(saw_release, "release inside lockout was dropped");
}

void test_hold_duration_tracks_the_press_edge(void) {
  Input input = freshInput();
  tick(input, 1000);
  TEST_ASSERT_EQUAL_UINT32(0, input.holdDuration(Button::kB));

  setButton(board::kPinButtonB, true);
  tick(input, 1100);
  tick(input, 2300);

  // 1200 ms is the exit gesture, so this boundary is load-bearing.
  TEST_ASSERT_TRUE(input.heldFor(Button::kB, 1200));
  TEST_ASSERT_EQUAL_UINT32(1200, input.holdDuration(Button::kB));

  setButton(board::kPinButtonB, false);
  tick(input, 2400);
  TEST_ASSERT_EQUAL_UINT32(0, input.holdDuration(Button::kB));
  TEST_ASSERT_FALSE(input.heldFor(Button::kB, 1));
}

void test_stick_deadzone_keeps_a_resting_hand_still(void) {
  Input input = freshInput();
  // Small wobble around the calibrated centre.
  setStick(0.52f);
  tick(input, 100);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, input.stickX());

  setStick(0.48f);
  tick(input, 116);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, input.stickX());
}

void test_stick_reaches_full_deflection_despite_deadzone(void) {
  Input input = freshInput();
  setStick(1.0f);
  tick(input, 100);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, input.stickX());

  setStick(0.0f);
  tick(input, 116);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, input.stickX());
}

void test_stick_inversion_flips_the_axis(void) {
  Input input = freshInput();
  input.setStickInverted(true);
  setStick(1.0f);
  tick(input, 100);
  TEST_ASSERT_TRUE(input.stickX() < -0.9f);
}

void test_nav_emits_one_step_per_flick(void) {
  Input input = freshInput();
  tick(input, 100);

  setStick(1.0f);
  tick(input, 116);
  TEST_ASSERT_EQUAL_INT8(1, input.navDelta());

  // Held past the threshold but inside the repeat delay: no further steps, or a
  // menu would run away on a single deliberate flick.
  int steps = 0;
  for (uint32_t t = 132; t < 500; t += 16) {
    tick(input, t);
    steps += input.navDelta() != 0 ? 1 : 0;
  }
  TEST_ASSERT_EQUAL_INT(0, steps);
}

void test_nav_auto_repeats_after_the_delay(void) {
  Input input = freshInput();
  tick(input, 100);
  setStick(1.0f);
  tick(input, 116);

  int steps = 0;
  for (uint32_t t = 132; t < 1400; t += 16) {
    tick(input, t);
    steps += input.navDelta() != 0 ? 1 : 0;
  }
  // ~900 ms of repeat at 140 ms per step.
  TEST_ASSERT_TRUE_MESSAGE(steps >= 4 && steps <= 8, "unexpected repeat rate");
}

void test_nav_hysteresis_prevents_a_stream_of_steps(void) {
  // A stick hovering between the release and step thresholds must not emit
  // anything; without hysteresis this is where a menu becomes unusable.
  Input input = freshInput();
  tick(input, 100);

  int steps = 0;
  for (uint32_t t = 116; t < 2000; t += 16) {
    // Oscillate in the band between kNavRelease (0.30) and kNavThreshold
    // (0.55).
    setStick(t % 32 == 0 ? 0.70f : 0.68f);
    tick(input, t);
    steps += input.navDelta() != 0 ? 1 : 0;
  }
  TEST_ASSERT_EQUAL_INT(0, steps);
}

void test_nav_steps_again_after_returning_to_centre(void) {
  Input input = freshInput();
  tick(input, 100);

  setStick(1.0f);
  tick(input, 116);
  TEST_ASSERT_EQUAL_INT8(1, input.navDelta());

  setStick(0.5f);
  tick(input, 132);
  TEST_ASSERT_EQUAL_INT8(0, input.navDelta());

  setStick(1.0f);
  tick(input, 148);
  TEST_ASSERT_EQUAL_INT8(1, input.navDelta());
}

void test_nav_reverses_immediately(void) {
  Input input = freshInput();
  tick(input, 100);

  setStick(1.0f);
  tick(input, 116);
  TEST_ASSERT_EQUAL_INT8(1, input.navDelta());

  // Slamming the other way must reverse without waiting for the repeat delay.
  setStick(0.0f);
  tick(input, 132);
  tick(input, 148);
  TEST_ASSERT_EQUAL_INT8(-1, input.navDelta());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_press_and_release_are_single_frame_events);
  RUN_TEST(test_fast_double_tap_is_not_swallowed_by_debounce);
  RUN_TEST(test_hold_duration_tracks_the_press_edge);
  RUN_TEST(test_stick_deadzone_keeps_a_resting_hand_still);
  RUN_TEST(test_stick_reaches_full_deflection_despite_deadzone);
  RUN_TEST(test_stick_inversion_flips_the_axis);
  RUN_TEST(test_nav_emits_one_step_per_flick);
  RUN_TEST(test_nav_auto_repeats_after_the_delay);
  RUN_TEST(test_nav_hysteresis_prevents_a_stream_of_steps);
  RUN_TEST(test_nav_steps_again_after_returning_to_centre);
  RUN_TEST(test_nav_reverses_immediately);
  return UNITY_END();
}
