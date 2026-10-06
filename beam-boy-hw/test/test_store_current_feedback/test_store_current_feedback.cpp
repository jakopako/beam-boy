#include <unity.h>

#include "scenes/store_current_feedback.h"

using namespace beamboy;

void setUp(void) {}
void tearDown(void) {}

void assertUnchanged(StoreCurrentFeedback& feedback, bool selected,
                     uint32_t now_ms) {
  Color color(240, 80, 20);
  float intensity = 0.15f;
  feedback.apply(selected, now_ms, color, intensity);
  TEST_ASSERT_EQUAL_UINT8(240, color.r);
  TEST_ASSERT_EQUAL_UINT8(80, color.g);
  TEST_ASSERT_EQUAL_UINT8(20, color.b);
  TEST_ASSERT_EQUAL_FLOAT(0.15f, intensity);
}

void test_only_selected_entry_turns_green(void) {
  StoreCurrentFeedback feedback;
  assertUnchanged(feedback, true, 0);
  feedback.begin(100);
  for (uint32_t elapsed = 0; elapsed < kStoreCurrentFeedbackMs; ++elapsed) {
    assertUnchanged(feedback, false, 100 + elapsed);
    Color color(240, 80, 20);
    float intensity = 0.8f;
    feedback.apply(true, 100 + elapsed, color, intensity);
    TEST_ASSERT_EQUAL_UINT8(kStoreOkColor.r, color.r);
    TEST_ASSERT_EQUAL_UINT8(kStoreOkColor.g, color.g);
    TEST_ASSERT_EQUAL_UINT8(kStoreOkColor.b, color.b);
    TEST_ASSERT_TRUE(intensity >= 0.35f);
    TEST_ASSERT_TRUE(intensity <= 1.0f);
  }
  assertUnchanged(feedback, true, 100 + kStoreCurrentFeedbackMs);
  assertUnchanged(feedback, true, 5000);
}

void test_two_green_pulses_last_1200_ms(void) {
  TEST_ASSERT_EQUAL_UINT32(1200, kStoreCurrentFeedbackMs);
  StoreCurrentFeedback feedback;
  feedback.begin(0);
  for (uint32_t elapsed : {uint32_t{0}, uint32_t{300}, uint32_t{600},
                           uint32_t{900}}) {
    Color color;
    float intensity = 0;
    feedback.apply(true, elapsed, color, intensity);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, elapsed % 600 == 0 ? 1.0f : 0.35f,
                            intensity);
  }
}

void test_navigation_cancels_and_repeat_press_restarts(void) {
  StoreCurrentFeedback feedback;
  feedback.begin(100);
  feedback.cancel();
  assertUnchanged(feedback, true, 200);
  feedback.begin(300);
  feedback.begin(1000);
  Color color;
  float intensity = 0;
  feedback.apply(true, 1000, color, intensity);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, intensity);
  assertUnchanged(feedback, true, 2200);
}

void test_confirmation_survives_clock_rollover(void) {
  StoreCurrentFeedback feedback;
  feedback.begin(UINT32_MAX - 599);
  Color color;
  float intensity = 0;
  feedback.apply(true, 0, color, intensity);
  TEST_ASSERT_EQUAL_UINT8(kStoreOkColor.g, color.g);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, intensity);
  assertUnchanged(feedback, true, 600);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_only_selected_entry_turns_green);
  RUN_TEST(test_two_green_pulses_last_1200_ms);
  RUN_TEST(test_navigation_cancels_and_repeat_press_restarts);
  RUN_TEST(test_confirmation_survives_clock_rollover);
  return UNITY_END();
}
