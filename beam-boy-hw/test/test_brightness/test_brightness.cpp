#include <LittleFS.h>
#include <unity.h>

#include "core/brightness_policy.h"
#include "scenes/brightness_scene.h"

using namespace beamboy;

namespace {

struct Fixture {
  Engine engine;
  BrightnessScene scene;
  uint32_t now_ms = 100;

  explicit Fixture(uint8_t stored = 0) {
    beamboy_host::state().reset();
    beamboy_host::files().clear();
    beamboy_host::state().pin_analog[board::kPinStickX] = board::kAdcMax / 2;
    beamboy_host::state().pin_analog[board::kPinStickY] = board::kAdcMax / 2;
    engine.input().begin();
    engine.display().begin();
    engine.storage().begin();
    engine.storage().setBrightness(stored);
    engine.storage().submitScore("wormfight", 42);
    engine.storage().setLastGameId("wormfight");
    engine.storage().commit();
    engine.display().setBrightness(brightness::fromStored(stored));
    scene.enter(engine);
  }

  void tick(uint32_t advance = 40) {
    now_ms += advance;
    beamboy_host::state().now_us = now_ms * 1000UL;
    engine.input().update(now_ms);
    scene.update(engine, 1.0f / 60.0f);
    scene.render(engine);
  }

  void stick(int8_t direction) {
    beamboy_host::state().pin_analog[board::kPinStickY] =
        direction < 0 ? 0 : (direction > 0 ? board::kAdcMax
                                          : board::kAdcMax / 2);
  }

  void step(int8_t direction) {
    stick(0);
    tick();
    stick(direction);
    tick();
  }

  void press(uint8_t pin) {
    beamboy_host::state().pin_digital[pin] = LOW;
    tick();
    beamboy_host::state().pin_digital[pin] = HIGH;
    tick();
  }
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_stored_brightness_is_safe_and_zero_means_default(void) {
  TEST_ASSERT_EQUAL_UINT8(64, brightness::fromStored(0));
  TEST_ASSERT_EQUAL_UINT8(128, board::kBrightnessCap);
  TEST_ASSERT_EQUAL_UINT8(32, brightness::kMinimum);
  TEST_ASSERT_EQUAL_UINT8(4, brightness::kStep);
  TEST_ASSERT_EQUAL_UINT8(brightness::kMinimum, brightness::fromStored(1));
  TEST_ASSERT_EQUAL_UINT8(32, brightness::fromStored(8));
  TEST_ASSERT_EQUAL_UINT8(32, brightness::fromStored(16));
  TEST_ASSERT_EQUAL_UINT8(32, brightness::fromStored(31));
  TEST_ASSERT_EQUAL_UINT8(32, brightness::fromStored(32));
  TEST_ASSERT_EQUAL_UINT8(37, brightness::fromStored(37));
  TEST_ASSERT_EQUAL_UINT8(board::kBrightnessCap, brightness::fromStored(255));
  for (int value = 0; value <= 255; ++value) {
    const uint8_t restored = brightness::fromStored(value);
    TEST_ASSERT_TRUE(restored >= brightness::kMinimum);
    TEST_ASSERT_TRUE(restored <= board::kBrightnessCap);
  }
}

void test_display_caps_every_setter_value_but_allows_intentional_black(void) {
  Display display;
  display.begin();
  for (int value = 0; value <= 255; ++value) {
    display.setBrightness(value);
    TEST_ASSERT_EQUAL_UINT8(value > board::kBrightnessCap
                               ? board::kBrightnessCap
                               : value,
                           display.brightness());
    display.rawPixel(0, colors::kWhite);
    display.present();
    TEST_ASSERT_TRUE(display.shownPixel(0).R <= board::kBrightnessCap);
  }
  display.setBrightness(0);
  display.present();
  TEST_ASSERT_EQUAL_UINT8(0, display.shownPixel(0).R);
}

void test_first_stick_step_changes_preview_immediately(void) {
  Fixture f;
  f.stick(-1);
  f.tick(16);
  TEST_ASSERT_EQUAL_UINT8(brightness::kDefault - brightness::kStep,
                         f.engine.display().brightness());
  TEST_ASSERT_TRUE(f.engine.storage().dirty());
}

void test_held_stick_uses_existing_repeat_delay_and_rate(void) {
  Fixture f;
  f.stick(-1);
  f.tick();
  f.tick(399);
  TEST_ASSERT_EQUAL_UINT8(brightness::kDefault - brightness::kStep,
                         f.engine.display().brightness());
  f.tick(1);
  TEST_ASSERT_EQUAL_UINT8(brightness::kDefault - 2 * brightness::kStep,
                         f.engine.display().brightness());
  f.tick(140);
  TEST_ASSERT_EQUAL_UINT8(brightness::kDefault - 3 * brightness::kStep,
                         f.engine.display().brightness());
}

void test_steps_stop_at_visible_minimum_and_safe_maximum(void) {
  Fixture f;
  for (int i = 0; i < 100; ++i) f.step(-1);
  TEST_ASSERT_EQUAL_UINT8(brightness::kMinimum,
                         f.engine.display().brightness());
  f.engine.display().present();
  TEST_ASSERT_EQUAL_UINT8(brightness::kMinimum,
                         f.engine.display().shownPixel(0).R);

  for (int i = 0; i < 100; ++i) f.step(1);
  TEST_ASSERT_EQUAL_UINT8(board::kBrightnessCap,
                         f.engine.display().brightness());
  f.engine.display().present();
  const RgbColor& marker =
      f.engine.display().shownPixel(board::kPixelCount - 1);
  TEST_ASSERT_EQUAL_UINT8(0, marker.R);
  TEST_ASSERT_TRUE(marker.B > 0);
}

void test_idle_and_render_do_not_change_preference(void) {
  Fixture f(32);
  const std::string saved = beamboy_host::files()["/beamboy.sav"];
  for (int i = 0; i < 60; ++i) f.tick(16);
  TEST_ASSERT_EQUAL_UINT8(32, f.engine.display().brightness());
  TEST_ASSERT_FALSE(f.engine.storage().dirty());
  TEST_ASSERT_TRUE(saved == beamboy_host::files()["/beamboy.sav"]);
}

void test_preview_scales_from_three_lower_leds_to_full_tube(void) {
  uint16_t previous_count = 0;
  for (int level = brightness::kMinimum; level <= board::kBrightnessCap;
       level += brightness::kStep) {
    Fixture f(static_cast<uint8_t>(level));
    f.scene.render(f.engine);
    f.engine.display().present();
    uint16_t lit_bar_pixels = 0;
    for (uint16_t i = 0; i < board::kPixelCount - 1; ++i) {
      const RgbColor& pixel = f.engine.display().shownPixel(i);
      if (pixel.R > 0) {
        ++lit_bar_pixels;
        TEST_ASSERT_EQUAL_UINT8(pixel.R, pixel.G);
        TEST_ASSERT_EQUAL_UINT8(pixel.R, pixel.B);
      } else {
        TEST_ASSERT_EQUAL_UINT8(0, pixel.G);
        TEST_ASSERT_EQUAL_UINT8(0, pixel.B);
      }
      if (level == brightness::kMinimum) {
        TEST_ASSERT_EQUAL_UINT8(i < 3 ? level : 0, pixel.R);
      }
    }
    TEST_ASSERT_TRUE(lit_bar_pixels >= previous_count);
    previous_count = lit_bar_pixels;
    if (level == board::kBrightnessCap) {
      TEST_ASSERT_EQUAL_UINT16(board::kPixelCount - 1, lit_bar_pixels);
    }
    const RgbColor& marker =
        f.engine.display().shownPixel(board::kPixelCount - 1);
    TEST_ASSERT_EQUAL_UINT8(0, marker.R);
    TEST_ASSERT_TRUE(marker.B > 0);
  }
}

void test_adjustments_stay_in_ram_until_a_confirms(void) {
  Fixture f;
  const std::string saved = beamboy_host::files()["/beamboy.sav"];
  f.step(-1);
  f.step(-1);
  TEST_ASSERT_TRUE(f.engine.storage().dirty());
  TEST_ASSERT_TRUE(saved == beamboy_host::files()["/beamboy.sav"]);
  Storage before_confirmation;
  before_confirmation.begin();
  TEST_ASSERT_EQUAL_UINT8(0, before_confirmation.brightness());

  f.press(board::kPinButtonA);
  TEST_ASSERT_FALSE(f.engine.storage().dirty());
  Storage reloaded;
  TEST_ASSERT_TRUE(reloaded.begin());
  TEST_ASSERT_EQUAL_UINT8(brightness::kDefault - 2 * brightness::kStep,
                         brightness::fromStored(reloaded.brightness()));
  TEST_ASSERT_EQUAL_UINT32(42, reloaded.highscore("wormfight"));
  TEST_ASSERT_EQUAL_STRING("wormfight", reloaded.lastGameId());
  f.engine.display().present();
  TEST_ASSERT_EQUAL_UINT8(0, f.engine.display().shownPixel(0).R);
  TEST_ASSERT_TRUE(f.engine.display().shownPixel(0).G > 0);
}

void test_leaving_editor_commits_without_a_confirmation(void) {
  Fixture f;
  f.step(-1);
  f.scene.exit(f.engine);
  Storage reloaded;
  reloaded.begin();
  TEST_ASSERT_FALSE(f.engine.storage().dirty());
  TEST_ASSERT_EQUAL_UINT8(f.engine.display().brightness(),
                         brightness::fromStored(reloaded.brightness()));
  f.scene.enter(f.engine);
  TEST_ASSERT_EQUAL_UINT8(reloaded.brightness(),
                         f.engine.display().brightness());
}

void test_pending_preview_is_available_to_engine_sleep_flush(void) {
  Fixture f;
  f.step(-1);
  TEST_ASSERT_TRUE(f.engine.storage().dirty());
  f.engine.storage().commit();
  Storage reloaded;
  reloaded.begin();
  TEST_ASSERT_EQUAL_UINT8(brightness::kDefault - brightness::kStep,
                         brightness::fromStored(reloaded.brightness()));
}

void test_stick_click_restores_default_without_erasing_other_data(void) {
  Fixture f(32);
  f.press(board::kPinStickSw);
  TEST_ASSERT_EQUAL_UINT8(brightness::kDefault,
                         f.engine.display().brightness());
  TEST_ASSERT_TRUE(f.engine.storage().dirty());
  f.scene.exit(f.engine);
  Storage reloaded;
  reloaded.begin();
  TEST_ASSERT_EQUAL_UINT8(brightness::kDefault,
                         brightness::fromStored(reloaded.brightness()));
  TEST_ASSERT_EQUAL_UINT32(42, reloaded.highscore("wormfight"));
}

void test_failed_save_keeps_pending_preference_and_shows_red(void) {
  Engine engine;
  BrightnessScene scene;
  beamboy_host::state().reset();
  beamboy_host::state().pin_analog[board::kPinStickX] = board::kAdcMax / 2;
  beamboy_host::state().pin_analog[board::kPinStickY] = board::kAdcMax / 2;
  engine.input().begin();
  engine.display().begin();
  scene.enter(engine);
  beamboy_host::state().pin_analog[board::kPinStickY] = 0;
  engine.input().update(100);
  scene.update(engine, 1.0f / 60.0f);
  beamboy_host::state().pin_digital[board::kPinButtonA] = LOW;
  engine.input().update(140);
  scene.update(engine, 1.0f / 60.0f);
  scene.render(engine);
  engine.display().present();
  TEST_ASSERT_TRUE(engine.storage().dirty());
  TEST_ASSERT_TRUE(engine.display().shownPixel(0).R > 0);
  TEST_ASSERT_EQUAL_UINT8(0, engine.display().shownPixel(0).G);
}

void test_hold_progress_is_visible_over_white_preview(void) {
  for (uint8_t level : {brightness::kMinimum, brightness::kDefault,
                        board::kBrightnessCap}) {
    Fixture f(level);
    f.scene.render(f.engine);
    f.engine.display().overlaySpan(0.0f, 0.5f, colors::kAmber, 0.8f);
    f.engine.display().present();
    const RgbColor& progress = f.engine.display().shownPixel(0);
    TEST_ASSERT_EQUAL_UINT8(level, progress.R);
    TEST_ASSERT_TRUE(progress.R > progress.G);
    TEST_ASSERT_TRUE(progress.G > progress.B);
    TEST_ASSERT_TRUE(progress.B < level / 2);
  }
}

void test_new_maximum_persists_and_default_reset_stays_64(void) {
  Fixture f;
  for (int i = 0; i < 100; ++i) f.step(1);
  f.scene.exit(f.engine);
  Storage reloaded;
  TEST_ASSERT_TRUE(reloaded.begin());
  TEST_ASSERT_EQUAL_UINT8(128, reloaded.brightness());
  TEST_ASSERT_EQUAL_UINT8(128, brightness::fromStored(reloaded.brightness()));
  f.scene.enter(f.engine);
  f.press(board::kPinStickSw);
  TEST_ASSERT_EQUAL_UINT8(64, f.engine.display().brightness());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_stored_brightness_is_safe_and_zero_means_default);
  RUN_TEST(test_display_caps_every_setter_value_but_allows_intentional_black);
  RUN_TEST(test_first_stick_step_changes_preview_immediately);
  RUN_TEST(test_held_stick_uses_existing_repeat_delay_and_rate);
  RUN_TEST(test_steps_stop_at_visible_minimum_and_safe_maximum);
  RUN_TEST(test_idle_and_render_do_not_change_preference);
  RUN_TEST(test_preview_scales_from_three_lower_leds_to_full_tube);
  RUN_TEST(test_adjustments_stay_in_ram_until_a_confirms);
  RUN_TEST(test_leaving_editor_commits_without_a_confirmation);
  RUN_TEST(test_pending_preview_is_available_to_engine_sleep_flush);
  RUN_TEST(test_stick_click_restores_default_without_erasing_other_data);
  RUN_TEST(test_failed_save_keeps_pending_preference_and_shows_red);
  RUN_TEST(test_hold_progress_is_visible_over_white_preview);
  RUN_TEST(test_new_maximum_persists_and_default_reset_stays_64);
  return UNITY_END();
}
