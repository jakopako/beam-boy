#include <unity.h>

#include "core/engine.h"

using namespace beamboy;

void setUp(void) { beamboy_host::state().reset(); }
void tearDown(void) {}

namespace {

void assertBlack(const Display& display, uint16_t pixel) {
  const RgbColor& c = display.shownPixel(pixel);
  TEST_ASSERT_EQUAL_UINT8(0, c.R);
  TEST_ASSERT_EQUAL_UINT8(0, c.G);
  TEST_ASSERT_EQUAL_UINT8(0, c.B);
}

void assertWhite(const Display& display, uint16_t pixel, uint8_t level) {
  const RgbColor& c = display.shownPixel(pixel);
  TEST_ASSERT_EQUAL_UINT8(level, c.R);
  TEST_ASSERT_EQUAL_UINT8(level, c.G);
  TEST_ASSERT_EQUAL_UINT8(level, c.B);
}

void assertSpacersBlack(const Display& display) {
  for (uint16_t i = 1; i < display.pixelCount(); i += 2) {
    assertBlack(display, i);
  }
}

}  // namespace

void test_five_is_colored_white_colored_with_dark_spacers(void) {
  Engine engine;
  Display& display = engine.display();
  display.setBrightness(128);
  for (uint16_t i = 0; i < display.pixelCount(); ++i) {
    display.rawPixel(i, colors::kWhite);
  }
  engine.renderScore(5, 0, true);
  display.present();
  TEST_ASSERT_EQUAL_UINT8(0, display.shownPixel(0).R);
  TEST_ASSERT_EQUAL_UINT8(128, display.shownPixel(0).B);
  assertWhite(display, 2, 32);
  TEST_ASSERT_EQUAL_UINT8(128, display.shownPixel(4).B);
  for (uint16_t i = 5; i < display.pixelCount(); ++i) assertBlack(display, i);
  assertSpacersBlack(display);
}

void test_zero_is_one_visible_white_dot_at_all_brightness_levels(void) {
  Engine engine;
  for (uint8_t level : {uint8_t{8}, uint8_t{64}, uint8_t{128}}) {
    engine.display().setBrightness(level);
    engine.renderScore(0, 0);
    engine.display().present();
    assertWhite(engine.display(), 0, level / 4);
    for (uint16_t i = 1; i < engine.display().pixelCount(); ++i) {
      assertBlack(engine.display(), i);
    }
  }
}

void test_animation_reveals_slots_and_keeps_zero_steady(void) {
  Engine engine;
  engine.display().setBrightness(128);
  engine.renderScore(5, 0);
  engine.display().present();
  TEST_ASSERT_TRUE(engine.display().shownPixel(0).B > 0);
  TEST_ASSERT_TRUE(engine.display().shownPixel(0).B < 128);
  assertBlack(engine.display(), 2);
  assertBlack(engine.display(), 4);

  engine.renderScore(5, 600);
  engine.display().present();
  TEST_ASSERT_EQUAL_UINT8(128, engine.display().shownPixel(0).B);
  assertWhite(engine.display(), 2, 32);
  assertBlack(engine.display(), 4);

  engine.renderScore(5, 1200);
  engine.display().present();
  assertWhite(engine.display(), 2, 32);
  TEST_ASSERT_TRUE(engine.display().shownPixel(4).B > 0);
  TEST_ASSERT_TRUE(engine.display().shownPixel(4).B < 128);

  engine.renderScore(5, Engine::kScoreRevealMs);
  engine.display().present();
  TEST_ASSERT_EQUAL_UINT8(128, engine.display().shownPixel(4).B);
  assertSpacersBlack(engine.display());
}

void test_every_one_bit_is_colored_and_nibbles_stay_grouped(void) {
  Engine engine;
  engine.display().setBrightness(128);
  engine.renderScore((uint32_t{1} << 25) - 1, 0, true);
  engine.display().present();
  for (uint16_t bit = 0; bit < 25; ++bit) {
    const RgbColor& c = engine.display().shownPixel(bit * 2);
    TEST_ASSERT_FALSE(c.R == c.G && c.G == c.B);
    if (bit % 4 != 0) {
      const RgbColor& previous = engine.display().shownPixel((bit - 1) * 2);
      TEST_ASSERT_EQUAL_UINT8(previous.R, c.R);
      TEST_ASSERT_EQUAL_UINT8(previous.G, c.G);
      TEST_ASSERT_EQUAL_UINT8(previous.B, c.B);
    }
  }
  assertSpacersBlack(engine.display());
}

void test_overflow_alternates_instead_of_truncating_and_recovers(void) {
  Engine engine;
  engine.display().setBrightness(128);
  for (uint32_t score : {uint32_t{1} << 25, UINT32_MAX}) {
    engine.renderScore(score, 0, true);
    engine.display().present();
    for (uint16_t i = 0; i < 50; i += 2) {
      TEST_ASSERT_EQUAL_UINT8(128, engine.display().shownPixel(i).R);
      TEST_ASSERT_EQUAL_UINT8(0, engine.display().shownPixel(i).B);
    }
    assertSpacersBlack(engine.display());
    engine.renderScore(score, 300, true);
    engine.display().present();
    for (uint16_t i = 0; i < 50; i += 2) {
      assertWhite(engine.display(), i, 128);
    }
    assertSpacersBlack(engine.display());
  }
  engine.renderScore(0, 0, true);
  engine.display().present();
  assertWhite(engine.display(), 0, 32);
  assertBlack(engine.display(), 2);
}

void test_reversal_mirrors_the_spaced_readout(void) {
  Engine engine;
  engine.display().setBrightness(128);
  engine.display().setReversed(true);
  engine.renderScore(5, 0, true);
  engine.display().present();
  TEST_ASSERT_EQUAL_UINT8(128, engine.display().shownPixel(49).B);
  assertWhite(engine.display(), 47, 32);
  TEST_ASSERT_EQUAL_UINT8(128, engine.display().shownPixel(45).B);
  for (uint16_t i = 0; i < 50; i += 2) assertBlack(engine.display(), i);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_five_is_colored_white_colored_with_dark_spacers);
  RUN_TEST(test_zero_is_one_visible_white_dot_at_all_brightness_levels);
  RUN_TEST(test_animation_reveals_slots_and_keeps_zero_steady);
  RUN_TEST(test_every_one_bit_is_colored_and_nibbles_stay_grouped);
  RUN_TEST(test_overflow_alternates_instead_of_truncating_and_recovers);
  RUN_TEST(test_reversal_mirrors_the_spaced_readout);
  return UNITY_END();
}
