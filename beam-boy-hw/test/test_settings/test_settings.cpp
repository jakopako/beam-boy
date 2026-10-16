#include <unity.h>

#include "scenes/settings_menu.h"

using namespace beamboy;

void setUp(void) {}
void tearDown(void) {}

void test_settings_has_four_children_in_planned_order(void) {
  TEST_ASSERT_EQUAL_UINT8(4, settings::kItemCount);
  TEST_ASSERT_EQUAL_UINT8(games::kNetworkUtilityIndex, settings::kItems[0]);
  TEST_ASSERT_EQUAL_UINT8(games::kUpdateFirmwareUtilityIndex, settings::kItems[1]);
  TEST_ASSERT_EQUAL_UINT8(games::kStoreUtilityIndex, settings::kItems[2]);
  TEST_ASSERT_EQUAL_UINT8(games::kBrightnessUtilityIndex, settings::kItems[3]);
  for (uint8_t i = 0; i < settings::kItemCount; ++i) {
    TEST_ASSERT_TRUE(settings::kItems[i] < games::kUtilityCount);
    TEST_ASSERT_TRUE(settings::kItems[i] != games::kSettingsUtilityIndex);
    for (uint8_t j = 0; j < i; ++j) {
      TEST_ASSERT_TRUE(settings::kItems[i] != settings::kItems[j]);
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_settings_has_four_children_in_planned_order);
  return UNITY_END();
}
