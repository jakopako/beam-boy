#pragma once

#include <stdint.h>

namespace beamboy {
namespace fw {

constexpr uint32_t kBootHealthWindowMs = 5000;

class BootHealth {
 public:
  void begin(bool pending_verification, bool startup_ready) {
    pending_verification_ = pending_verification;
    startup_ready_ = startup_ready;
    launcher_window_started_ = false;
  }

  float progress(uint32_t now_ms, bool launcher_active) {
    if (!pending_verification_ || !startup_ready_) return 0.0f;
    if (!launcher_active) {
      launcher_window_started_ = false;
      return 0.0f;
    }
    if (!launcher_window_started_) {
      launcher_started_ms_ = now_ms;
      launcher_window_started_ = true;
      return 0.0f;
    }
    const uint32_t elapsed_ms = now_ms - launcher_started_ms_;
    if (elapsed_ms >= kBootHealthWindowMs) return 1.0f;
    return static_cast<float>(elapsed_ms) / kBootHealthWindowMs;
  }

  bool observe(uint32_t now_ms, bool launcher_active) {
    return progress(now_ms, launcher_active) >= 1.0f;
  }

 private:
  bool pending_verification_ = false;
  bool startup_ready_ = false;
  bool launcher_window_started_ = false;
  uint32_t launcher_started_ms_ = 0;
};

}  // namespace fw
}  // namespace beamboy
