#include <LittleFS.h>
#include <unity.h>

#include "core/run_score.h"

using namespace beamboy;

void setUp(void) { beamboy_host::files().clear(); }
void tearDown(void) {}

void test_restart_clears_run_and_preserves_best_score_for_exit_commit(void) {
  Storage storage;
  TEST_ASSERT_TRUE(storage.begin());
  storage.setBrightness(128);
  storage.submitScore("wormfight", 42);
  storage.commit();
  const auto saved_files = beamboy_host::files();

  uint32_t score = 17;
  TEST_ASSERT_TRUE(resetRunScore(storage, "reflex", score));
  TEST_ASSERT_EQUAL_UINT32(0, score);
  TEST_ASSERT_EQUAL_UINT32(17, storage.highscore("reflex"));
  TEST_ASSERT_TRUE(storage.dirty());
  TEST_ASSERT_TRUE(saved_files == beamboy_host::files());

  score += 3;
  TEST_ASSERT_EQUAL_UINT32(3, score);
  TEST_ASSERT_TRUE(resetRunScore(storage, "reflex", score));
  TEST_ASSERT_EQUAL_UINT32(0, score);
  TEST_ASSERT_EQUAL_UINT32(17, storage.highscore("reflex"));
  storage.commit();
  Storage reloaded;
  TEST_ASSERT_TRUE(reloaded.begin());
  TEST_ASSERT_EQUAL_UINT32(17, reloaded.highscore("reflex"));
  TEST_ASSERT_EQUAL_UINT32(42, reloaded.highscore("wormfight"));
  TEST_ASSERT_EQUAL_UINT8(128, reloaded.brightness());
}

void test_better_retry_updates_highscore_without_accumulating_runs(void) {
  Storage storage;
  storage.begin();
  uint32_t score = 5;
  TEST_ASSERT_TRUE(resetRunScore(storage, "reflex", score));
  score += 8;
  TEST_ASSERT_TRUE(resetRunScore(storage, "reflex", score));
  TEST_ASSERT_EQUAL_UINT32(0, score);
  TEST_ASSERT_EQUAL_UINT32(8, storage.highscore("reflex"));
}

void test_initial_zero_reset_creates_no_score_or_write(void) {
  Storage storage;
  storage.begin();
  storage.commit();
  uint32_t score = 0;
  TEST_ASSERT_TRUE(resetRunScore(storage, "reflex", score));
  TEST_ASSERT_EQUAL_UINT32(0, score);
  TEST_ASSERT_EQUAL_UINT32(0, storage.highscore("reflex"));
  TEST_ASSERT_FALSE(storage.dirty());
}

void test_failed_score_preservation_does_not_destroy_the_run(void) {
  Storage storage;
  storage.begin();
  uint32_t score = 17;
  TEST_ASSERT_FALSE(resetRunScore(storage, nullptr, score));
  TEST_ASSERT_EQUAL_UINT32(17, score);
  TEST_ASSERT_FALSE(resetRunScore(storage, "", score));
  TEST_ASSERT_EQUAL_UINT32(17, score);
  TEST_ASSERT_FALSE(resetRunScore(storage, "id-too-long-for-storage", score));
  TEST_ASSERT_EQUAL_UINT32(17, score);

  for (int i = 0; i < 24; ++i) {
    char id[12];
    snprintf(id, sizeof(id), "game%d", i);
    TEST_ASSERT_TRUE(storage.submitScore(id, 1));
  }
  TEST_ASSERT_FALSE(resetRunScore(storage, "reflex", score));
  TEST_ASSERT_EQUAL_UINT32(17, score);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_restart_clears_run_and_preserves_best_score_for_exit_commit);
  RUN_TEST(test_better_retry_updates_highscore_without_accumulating_runs);
  RUN_TEST(test_initial_zero_reset_creates_no_score_or_write);
  RUN_TEST(test_failed_score_preservation_does_not_destroy_the_run);
  return UNITY_END();
}
