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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_erase_score_forgets_a_known_game);
  RUN_TEST(test_erase_score_survives_a_save_reload);
  RUN_TEST(test_erase_score_does_not_disturb_other_games);
  RUN_TEST(test_erase_score_on_an_unknown_game_is_a_harmless_no_op);
  RUN_TEST(test_erase_score_frees_its_slot_for_reuse);
  return UNITY_END();
}
