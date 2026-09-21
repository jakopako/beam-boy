#include <unity.h>

#include "core/launcher_policy.h"

using namespace beamboy;

void setUp(void) {}
void tearDown(void) {}

void test_launcher_contract_is_nine_games_plus_settings(void) {
  TEST_ASSERT_EQUAL_UINT8(9, kMaxGames);
  TEST_ASSERT_EQUAL_UINT8(10, kLauncherSlotCount);
  TEST_ASSERT_EQUAL_UINT8(9, kSettingsSlot);
}

void test_games_keep_their_dense_left_aligned_slots(void) {
  TEST_ASSERT_EQUAL_UINT8(0, launcherSlotForSelection(0, 3));
  TEST_ASSERT_EQUAL_UINT8(1, launcherSlotForSelection(1, 3));
  TEST_ASSERT_EQUAL_UINT8(2, launcherSlotForSelection(2, 3));
}

void test_settings_is_always_at_the_final_slot(void) {
  TEST_ASSERT_EQUAL_UINT8(9, launcherSlotForSelection(1, 1));
  TEST_ASSERT_EQUAL_UINT8(9, launcherSlotForSelection(3, 3));
  TEST_ASSERT_EQUAL_UINT8(9, launcherSlotForSelection(9, 9));
}

void test_empty_slots_between_games_and_settings_have_pixel_ranges(void) {
  TEST_ASSERT_EQUAL_UINT16(0, launcherSlotStart(0, 50));
  TEST_ASSERT_EQUAL_UINT16(5, launcherSlotEnd(0, 50));
  TEST_ASSERT_EQUAL_UINT16(45, launcherSlotStart(kSettingsSlot, 50));
  TEST_ASSERT_EQUAL_UINT16(50, launcherSlotEnd(kSettingsSlot, 50));
}

void test_new_installs_stop_at_nine_but_updates_continue(void) {
  TEST_ASSERT_TRUE(canInstallGame(8, false));
  TEST_ASSERT_FALSE(canInstallGame(9, false));
  TEST_ASSERT_TRUE(canInstallGame(9, true));
}

void test_built_in_games_reduce_available_cartridge_slots(void) {
  TEST_ASSERT_EQUAL_UINT8(8, maxInstalledGames(1));
  TEST_ASSERT_EQUAL_UINT8(0, maxInstalledGames(9));
  TEST_ASSERT_EQUAL_UINT8(0, maxInstalledGames(10));
}

void test_navigation_steps_directly_between_last_game_and_settings(void) {
  TEST_ASSERT_EQUAL_UINT8(3, moveLauncherSelection(2, 3, 1));
  TEST_ASSERT_EQUAL_UINT8(2, moveLauncherSelection(3, 3, -1));
  TEST_ASSERT_EQUAL_UINT8(3, moveLauncherSelection(3, 3, 1));
  TEST_ASSERT_EQUAL_UINT8(0, moveLauncherSelection(0, 3, -1));
}

void test_rebuild_keeps_settings_selected_at_its_new_dense_index(void) {
  TEST_ASSERT_EQUAL_UINT8(5, reconcileLauncherSelection(3, true, 5));
}

void test_rebuild_clamps_a_removed_last_game_to_the_previous_game(void) {
  TEST_ASSERT_EQUAL_UINT8(2, reconcileLauncherSelection(3, false, 3));
  TEST_ASSERT_EQUAL_UINT8(0, reconcileLauncherSelection(0, false, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_launcher_contract_is_nine_games_plus_settings);
  RUN_TEST(test_games_keep_their_dense_left_aligned_slots);
  RUN_TEST(test_settings_is_always_at_the_final_slot);
  RUN_TEST(test_empty_slots_between_games_and_settings_have_pixel_ranges);
  RUN_TEST(test_new_installs_stop_at_nine_but_updates_continue);
  RUN_TEST(test_built_in_games_reduce_available_cartridge_slots);
  RUN_TEST(test_navigation_steps_directly_between_last_game_and_settings);
  RUN_TEST(test_rebuild_keeps_settings_selected_at_its_new_dense_index);
  RUN_TEST(test_rebuild_clamps_a_removed_last_game_to_the_previous_game);
  return UNITY_END();
}
