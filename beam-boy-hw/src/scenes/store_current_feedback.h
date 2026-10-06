#pragma once

#include "core/display.h"

namespace beamboy {

constexpr Color kStoreOkColor(0, 255, 60);
constexpr uint32_t kStoreCurrentFeedbackMs = 1200;

class StoreCurrentFeedback {
 public:
  void begin(uint32_t now_ms) {
    started_at_ms_ = now_ms;
    active_ = true;
  }

  void cancel() { active_ = false; }

  void apply(bool selected, uint32_t now_ms, Color& color, float& intensity) {
    if (!active_) return;
    const uint32_t elapsed_ms = now_ms - started_at_ms_;
    if (elapsed_ms >= kStoreCurrentFeedbackMs) {
      cancel();
      return;
    }
    if (!selected) return;

    color = kStoreOkColor;
    const float phase = 1.57079633f + 12.56637061f * elapsed_ms /
                                         kStoreCurrentFeedbackMs;
    intensity = 0.35f + 0.65f * (0.5f + 0.5f * wrappedSin(phase));
  }

 private:
  uint32_t started_at_ms_ = 0;
  bool active_ = false;
};

}  // namespace beamboy
