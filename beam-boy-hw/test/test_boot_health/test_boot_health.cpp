#include <unity.h>

#include "core/boot_health.h"

using beamboy::fw::BootHealth;
using beamboy::fw::kBootHealthWindowMs;

void setUp(void) {}
void tearDown(void) {}

void test_normal_boot_never_requests_ota_acknowledgement() {
  BootHealth health;
  health.begin(false, true);

  TEST_ASSERT_FALSE(health.observe(0, true));
  TEST_ASSERT_FALSE(health.observe(kBootHealthWindowMs + 1, true));
}

void test_unhealthy_startup_never_requests_acknowledgement() {
  BootHealth health;
  health.begin(true, false);

  TEST_ASSERT_FALSE(health.observe(0, true));
  TEST_ASSERT_FALSE(health.observe(kBootHealthWindowMs + 1, true));
}

void test_launcher_must_run_for_the_full_health_window() {
  BootHealth health;
  health.begin(true, true);

  TEST_ASSERT_FALSE(health.observe(100, true));
  TEST_ASSERT_FALSE(health.observe(100 + kBootHealthWindowMs - 1, true));
  TEST_ASSERT_TRUE(health.observe(100 + kBootHealthWindowMs, true));
}

void test_progress_advances_during_a_continuous_launcher_window() {
  BootHealth health;
  health.begin(true, true);

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, health.progress(100, true));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f,
                           health.progress(100 + 1000, true));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f,
                           health.progress(100 + 3000, true));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f,
                           health.progress(100 + kBootHealthWindowMs, true));
}

void test_leaving_launcher_restarts_the_health_window() {
  BootHealth health;
  health.begin(true, true);

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, health.progress(100, true));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, health.progress(200, false));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, health.progress(300, true));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 0.9998f,
      health.progress(300 + kBootHealthWindowMs - 1, true));
  TEST_ASSERT_TRUE(health.observe(300 + kBootHealthWindowMs, true));
}

void test_health_window_handles_millis_rollover() {
  BootHealth health;
  health.begin(true, true);
  const uint32_t start = UINT32_MAX - 1000;

  TEST_ASSERT_FALSE(health.observe(start, true));
  TEST_ASSERT_FALSE(health.observe(start + kBootHealthWindowMs - 1, true));
  TEST_ASSERT_TRUE(health.observe(start + kBootHealthWindowMs, true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_normal_boot_never_requests_ota_acknowledgement);
  RUN_TEST(test_unhealthy_startup_never_requests_acknowledgement);
  RUN_TEST(test_launcher_must_run_for_the_full_health_window);
  RUN_TEST(test_progress_advances_during_a_continuous_launcher_window);
  RUN_TEST(test_leaving_launcher_restarts_the_health_window);
  RUN_TEST(test_health_window_handles_millis_rollover);
  return UNITY_END();
}
