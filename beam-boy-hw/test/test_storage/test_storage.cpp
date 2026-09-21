// Beam Boy — host tests for persistent storage.
//
// The one behaviour worth pinning down beyond load/save round-tripping is
// eraseScore(): deleting a cartridge must not leave its highscore behind, or
// a later game that happens to reuse the same id would silently inherit a
// score it never earned.

#include <LittleFS.h>
#include <unity.h>

#include "core/storage.h"

using namespace beamboy;

namespace {

Storage freshStorage() {
  beamboy_host::files().clear();
  Storage storage;
  storage.begin();
  return storage;
}

struct LegacyScoreEntry {
  char game_id[12] = {0};
  uint32_t score = 0;
};

struct LegacySaveData {
  uint8_t version = 2;
  uint8_t brightness = 0;
  uint8_t last_game = 0;
  uint8_t score_count = 0;
  LegacyScoreEntry scores[24];
};

uint32_t legacyChecksum(const LegacySaveData& data) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(data); i++) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_erase_score_forgets_a_known_game(void) {
  Storage storage = freshStorage();
  TEST_ASSERT_TRUE(storage.submitScore("wormfight", 42));
  TEST_ASSERT_EQUAL_UINT32(42, storage.highscore("wormfight"));

  storage.eraseScore("wormfight");

  TEST_ASSERT_EQUAL_UINT32(0, storage.highscore("wormfight"));
}

void test_erase_score_survives_a_save_reload(void) {
  Storage storage = freshStorage();
  TEST_ASSERT_TRUE(storage.submitScore("wormfight", 42));
  storage.eraseScore("wormfight");
  storage.commit();

  Storage reloaded;
  reloaded.begin();
  TEST_ASSERT_EQUAL_UINT32(0, reloaded.highscore("wormfight"));
}

void test_erase_score_does_not_disturb_other_games(void) {
  Storage storage = freshStorage();
  TEST_ASSERT_TRUE(storage.submitScore("wormfight", 42));
  TEST_ASSERT_TRUE(storage.submitScore("reflexfs", 7));

  storage.eraseScore("wormfight");

  TEST_ASSERT_EQUAL_UINT32(0, storage.highscore("wormfight"));
  TEST_ASSERT_EQUAL_UINT32(7, storage.highscore("reflexfs"));
}

void test_erase_score_on_an_unknown_game_is_a_harmless_no_op(void) {
  Storage storage = freshStorage();
  TEST_ASSERT_TRUE(storage.submitScore("wormfight", 42));
  storage.commit();

  storage.eraseScore("no-such-game");

  TEST_ASSERT_EQUAL_UINT32(42, storage.highscore("wormfight"));
  TEST_ASSERT_FALSE(storage.dirty());
}

// A freed slot must be reusable, not merely blanked -- otherwise repeated
// delete/install cycles would eventually exhaust kMaxScores even though most
// of the "used" slots hold nothing.
void test_erase_score_frees_its_slot_for_reuse(void) {
  Storage storage = freshStorage();
  TEST_ASSERT_TRUE(storage.submitScore("wormfight", 42));
  storage.eraseScore("wormfight");

  TEST_ASSERT_TRUE(storage.submitScore("newgame", 1));
  TEST_ASSERT_EQUAL_UINT32(1, storage.highscore("newgame"));
}

void test_last_game_id_survives_a_save_reload(void) {
  Storage storage = freshStorage();
  storage.setLastGame(3);
  storage.setLastGameId("reflexfs");
  storage.commit();

  Storage reloaded;
  reloaded.begin();
  TEST_ASSERT_EQUAL_UINT8(3, reloaded.lastGame());
  TEST_ASSERT_EQUAL_STRING("reflexfs", reloaded.lastGameId());
}

void test_v2_save_migrates_without_losing_settings_or_scores(void) {
  beamboy_host::files().clear();

  LegacySaveData legacy;
  legacy.brightness = 37;
  legacy.last_game = 2;
  legacy.score_count = 1;
  strcpy(legacy.scores[0].game_id, "wormfight");
  legacy.scores[0].score = 81;
  const uint32_t sum = legacyChecksum(legacy);

  std::string bytes(reinterpret_cast<const char*>(&legacy), sizeof(legacy));
  bytes.append(reinterpret_cast<const char*>(&sum), sizeof(sum));
  beamboy_host::files()["/beamboy.sav"] = bytes;

  Storage migrated;
  TEST_ASSERT_TRUE(migrated.begin());
  TEST_ASSERT_EQUAL_UINT8(37, migrated.brightness());
  TEST_ASSERT_EQUAL_UINT8(2, migrated.lastGame());
  TEST_ASSERT_EQUAL_STRING("", migrated.lastGameId());
  TEST_ASSERT_EQUAL_UINT32(81, migrated.highscore("wormfight"));
  TEST_ASSERT_TRUE(migrated.dirty());

  migrated.commit();
  Storage reloaded;
  TEST_ASSERT_TRUE(reloaded.begin());
  TEST_ASSERT_EQUAL_UINT8(37, reloaded.brightness());
  TEST_ASSERT_EQUAL_UINT32(81, reloaded.highscore("wormfight"));
  TEST_ASSERT_FALSE(reloaded.dirty());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_erase_score_forgets_a_known_game);
  RUN_TEST(test_erase_score_survives_a_save_reload);
  RUN_TEST(test_erase_score_does_not_disturb_other_games);
  RUN_TEST(test_erase_score_on_an_unknown_game_is_a_harmless_no_op);
  RUN_TEST(test_erase_score_frees_its_slot_for_reuse);
  RUN_TEST(test_last_game_id_survives_a_save_reload);
  RUN_TEST(test_v2_save_migrates_without_losing_settings_or_scores);
  return UNITY_END();
}
