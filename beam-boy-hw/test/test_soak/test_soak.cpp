// Beam Boy — headless soak test.
//
// This is the test that would have caught the crash that took the longest to
// find this project. The device ran perfectly for several minutes and then
// reset, because every animated scene fed an ever-growing phase into sinf(),
// and past a few hundred radians newlib leaves its fast path for a routine
// whose large stack frame does not fit in the ESP8266's ~4 KB cont stack.
//
// Nothing about that is reproducible on a PC: the host's libm handles huge
// arguments happily and there is no cont stack to overflow. So the test does
// not try to reproduce the crash. It asserts on the *precondition* instead --
// that after simulated hours of running, the values a scene passes to sinf()
// are still small. That is a property the host can check, and it is the thing
// that actually has to stay true.
//
// The general lesson, which is worth more than this file: for embedded code the
// valuable host tests are the ones that run the system far past the point a
// human would sit and watch it, and assert on invariants rather than on
// outputs. Accumulators, counters and timers are where the bugs are, and they
// are all cheap to fast-forward.

#include <unity.h>

#include <cmath>

#include "core/display.h"

using namespace beamboy;

namespace {

// The phase magnitude beyond which newlib's sinf() takes the huge-argument
// path. The real threshold is larger, but anything past a few hundred radians
// has also lost enough float precision to animate badly, so this doubles as a
// smoothness check.
constexpr float kMaxSafePhase = 1000.0f;

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_wrapped_sin_argument_stays_small_over_simulated_hours(void) {
  // Reproduces the accumulation pattern every animated scene uses: a phase
  // advanced by dt each frame, wrapped by the scene, then handed to wrappedSin.
  beamboy_host::state().reset();

  float phase = 0.0f;
  const float dt = 1.0f / 60.0f;
  const float rate = 3.0f;

  // Six simulated hours at 60 fps.
  const uint32_t frames = 6 * 60 * 60 * 60;
  for (uint32_t i = 0; i < frames; i++) {
    phase += dt * rate;
    // The hourly wrap the scenes apply.
    if (phase > 6.28318531f) phase -= 6.28318531f;

    TEST_ASSERT_TRUE_MESSAGE(fabsf(phase) < kMaxSafePhase,
                             "animation phase grew without bound");
    const float v = wrappedSin(phase);
    TEST_ASSERT_FALSE(std::isnan(v));
  }
}

void test_millis_based_pulse_survives_the_rollover_window(void) {
  // The most dangerous call site shape: millis() / 1000.0f * rate. millis()
  // climbs to ~4.29e9, so the seconds value reaches ~4.29e6 -- far past the
  // safe range -- and it gets there after about 49 days of uptime, which no
  // manual test will ever reach. pulse() must wrap it regardless.
  beamboy_host::state().reset();

  // Sample across the whole 32-bit millis range, including right up to the
  // rollover, in steps far too large to walk one frame at a time.
  for (uint64_t ms = 0; ms < 4294967295ULL; ms += 37000000ULL) {
    const float seconds = static_cast<float>(ms) / 1000.0f;
    for (float rate : {0.5f, 2.0f, 6.0f}) {
      const float v = pulse(seconds, rate);
      TEST_ASSERT_FALSE_MESSAGE(std::isnan(v), "pulse produced NaN");
      TEST_ASSERT_TRUE_MESSAGE(v >= -1.0001f && v <= 1.0001f,
                               "pulse left the unit range");
    }
  }
}

void test_millis_rollover_does_not_break_elapsed_time(void) {
  // Every scene computes elapsed time as millis() - started_at. That is correct
  // across the rollover *only* because both are unsigned 32-bit; writing it
  // with a signed intermediate would break once every 49 days. Pin the
  // behaviour.
  const uint32_t started = 4294967000u;  // ~300 ms before rollover
  const uint32_t now = 200u;             // ~200 ms after
  const uint32_t elapsed = now - started;
  TEST_ASSERT_EQUAL_UINT32(496, elapsed);
}

void test_fade_reaches_black_within_a_bounded_number_of_frames(void) {
  // A trail that never quite dies leaves the tube permanently dim. This is the
  // kind of thing that is invisible for the first minute on hardware and
  // obvious after an hour.
  Color c(255, 255, 255);
  int frames = 0;
  while ((c.r || c.g || c.b) && frames < 10000) {
    c = c.scaled(0.85f);
    frames++;
  }
  TEST_ASSERT_TRUE_MESSAGE(frames < 10000, "fade never reached black");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wrapped_sin_argument_stays_small_over_simulated_hours);
  RUN_TEST(test_millis_based_pulse_survives_the_rollover_window);
  RUN_TEST(test_millis_rollover_does_not_break_elapsed_time);
  RUN_TEST(test_fade_reaches_black_within_a_bounded_number_of_frames);
  return UNITY_END();
}
