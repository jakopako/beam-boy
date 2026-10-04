#include <LittleFS.h>
#include <unity.h>

#include "scenes/settings_menu.h"
#include "scenes/update_firmware_scene.h"

using namespace beamboy;

void setUp(void) {
  beamboy_host::state().reset();
  beamboy_host::files().clear();
}
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

void test_placeholder_preserves_preferences_and_credentials(void) {
  Engine engine;
  UpdateFirmwareScene scene;
  engine.display().begin();
  engine.display().setBrightness(128);
  beamboy_host::state().pin_analog[board::kPinStickX] = board::kAdcMax / 2;
  beamboy_host::state().pin_analog[board::kPinStickY] = board::kAdcMax / 2;
  engine.input().begin();
  engine.storage().begin();
  engine.storage().setBrightness(128);
  engine.storage().submitScore("wormfight", 42);
  engine.storage().commit();
  beamboy_host::files()["/net.cfg"] = "test-network\ntest-password\n";
  const auto saved_files = beamboy_host::files();

  scene.enter(engine);
  for (uint32_t i = 1; i <= 60; ++i) {
    beamboy_host::state().now_us = i * 40000UL;
    beamboy_host::state().pin_digital[board::kPinButtonA] =
        i % 2 ? LOW : HIGH;
    engine.input().update(millis());
    scene.update(engine, 1.0f / 60.0f);
    scene.render(engine);
  }
  scene.exit(engine);
  TEST_ASSERT_EQUAL_UINT8(128, engine.display().brightness());
  TEST_ASSERT_FALSE(engine.storage().dirty());
  TEST_ASSERT_TRUE(saved_files == beamboy_host::files());
}

void test_placeholder_draws_two_amber_bars_with_a_dark_gap(void) {
  Engine engine;
  UpdateFirmwareScene scene;
  engine.display().begin();
  engine.display().setBrightness(128);
  scene.enter(engine);
  scene.render(engine);
  engine.display().present();

  const RgbColor& left =
      engine.display().shownPixel(board::kPixelCount / 5);
  const RgbColor& right =
      engine.display().shownPixel(board::kPixelCount * 4 / 5);
  TEST_ASSERT_TRUE(left.R > left.G);
  TEST_ASSERT_TRUE(left.G > 0);
  TEST_ASSERT_EQUAL_UINT8(0, left.B);
  TEST_ASSERT_TRUE(right.R > right.G);
  TEST_ASSERT_TRUE(right.G > 0);
  TEST_ASSERT_EQUAL_UINT8(0, right.B);
  const RgbColor& gap =
      engine.display().shownPixel(board::kPixelCount / 2);
  TEST_ASSERT_EQUAL_UINT8(0, gap.R);
  TEST_ASSERT_EQUAL_UINT8(0, gap.G);
  TEST_ASSERT_EQUAL_UINT8(0, gap.B);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_settings_has_four_children_in_planned_order);
  RUN_TEST(test_placeholder_preserves_preferences_and_credentials);
  RUN_TEST(test_placeholder_draws_two_amber_bars_with_a_dark_gap);
  return UNITY_END();
}
