#pragma once

#include "core/display.h"
#include "scenes/launcher_gestures.h"

namespace beamboy {

constexpr uint32_t kDeleteConfirmationMs = 400;

struct DeleteFeedback {
  Color color;
  float intensity;
};

inline float deletePulseRate(float progress) {
  return 0.64f + 4.36f * progress;
}

inline DeleteFeedback launcherDeleteFeedback(const Color& accent,
                                             uint32_t held_ms) {
  if (held_ms >= kDeleteHoldMs) return {colors::kRed, 1.0f};
  const float progress = static_cast<float>(held_ms) / kDeleteHoldMs;
  const float seconds = held_ms / 1000.0f;
  // Integrate the increasing rate so acceleration does not introduce jumps.
  const float cycles =
      seconds * (deletePulseRate(0.0f) + deletePulseRate(progress)) * 0.5f;
  const float pulse_level = 0.5f + 0.5f * wrappedSin(cycles * 6.28318531f);
  const float minimum = 0.5f + 0.5f * progress;
  const Color color(
      static_cast<uint8_t>(accent.r + (255 - accent.r) * progress),
      static_cast<uint8_t>(accent.g * (1.0f - progress)),
      static_cast<uint8_t>(accent.b * (1.0f - progress)));
  return {color, minimum + (1.0f - minimum) * pulse_level};
}

}  // namespace beamboy
