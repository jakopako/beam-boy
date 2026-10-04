#include "engine.h"

namespace beamboy {
namespace {

const Color kNibbleColors[] = {
    Color(0, 150, 255),   // blue
    Color(0, 220, 80),    // green
    Color(255, 170, 0),   // amber
    Color(255, 40, 90),   // red
    Color(190, 90, 255),  // violet
    Color(0, 255, 200),   // turquoise; white is reserved for zero
};
constexpr uint8_t kNibbleColorCount =
    sizeof(kNibbleColors) / sizeof(kNibbleColors[0]);
constexpr float kZeroBitIntensity = 0.25f;

// Keep float literals out of the render loop: inlining this triggered an
// Xtensa GCC postreload internal compiler error on the ESP32 toolchain.
float __attribute__((noinline)) bitIntensity(uint16_t bit, uint32_t revealed,
                                            uint32_t elapsed_ms,
                                            uint32_t per_bit_ms) {
  if (bit != revealed || per_bit_ms == 0) return 1.0f;
  const float progress = static_cast<float>(elapsed_ms % per_bit_ms) /
                         static_cast<float>(per_bit_ms);
  return 0.35f + 0.65f * progress;
}

}  // namespace

void Engine::renderScore(uint32_t score, uint32_t elapsed_ms, bool instant) {
  // Own the entire readout, including dark spacers and unrevealed slots.
  display_.clear();
  const uint16_t capacity = (display_.pixelCount() + 1) / 2;
  uint8_t significant_bits = 1;
  for (uint8_t bit = 0; bit < 32; ++bit) {
    if (score & (uint32_t{1} << bit)) significant_bits = bit + 1;
  }

  if (significant_bits > capacity) {
    if (!score_overflow_logged_ || score != last_overflow_score_) {
      Serial.print("[score] display overflow; full score: ");
      Serial.println(static_cast<unsigned long>(score));
      last_overflow_score_ = score;
      score_overflow_logged_ = true;
    }
    // Alternating whole-readout amber/white dots are not a valid bit pattern.
    const Color color =
        (elapsed_ms / 300) % 2 == 0 ? colors::kAmber : colors::kWhite;
    for (uint16_t bit = 0; bit < capacity; ++bit) {
      display_.rawPixel(bit * 2, color);
    }
    return;
  }
  score_overflow_logged_ = false;

  const uint32_t per_bit_ms = kScoreRevealMs / significant_bits;
  const uint32_t revealed = elapsed_ms / per_bit_ms;
  for (uint16_t bit = 0; bit < significant_bits; ++bit) {
    if (!instant && bit > revealed) break;
    const bool set = (score & (uint32_t{1} << bit)) != 0;
    // Zeros are steady position markers; only arriving ones fade up.
    drawScoreBit(bit, set,
                 instant || !set
                     ? 1.0f
                     : bitIntensity(bit, revealed, elapsed_ms, per_bit_ms));
  }
}

void Engine::drawScoreBit(uint16_t bit, bool set, float intensity) {
  const Color color =
      set ? kNibbleColors[(bit / 4) % kNibbleColorCount] : colors::kWhite;
  display_.rawPixel(bit * 2,
                    color.scaled(set ? intensity : kZeroBitIntensity));
}

}  // namespace beamboy
