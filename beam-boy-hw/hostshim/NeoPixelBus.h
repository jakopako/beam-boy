#pragma once

// Beam Boy — NeoPixelBus shim for host builds.
//
// Captures what would have been written to the strip so tests can assert on
// rendered output, which is the only way to check drawing code off-target.
// Timing and the DMA/RMT machinery are not modelled at all; those only ever
// misbehave on real hardware.

#include <cstdint>
#include <vector>

struct RgbColor {
  uint8_t R = 0;
  uint8_t G = 0;
  uint8_t B = 0;

  RgbColor() = default;
  RgbColor(uint8_t r, uint8_t g, uint8_t b) : R(r), G(g), B(b) {}
};

struct NeoGrbFeature {};
struct NeoNativeMethod {};

template <typename Feature, typename Method>
class NeoPixelBus {
 public:
  NeoPixelBus(uint16_t count, uint8_t pin) : pixels_(count), pin_(pin) {}

  void Begin() {}
  void Show() {
    shown_ = pixels_;
    ++shown_count_;
  }

  // Real hardware returns false while the previous transfer is still draining.
  // The host has no DMA, so it is always ready -- this keeps Display::present()
  // testable without the tests having to model transfer timing.
  bool CanShow() const { return true; }

  void SetPixelColor(uint16_t index, const RgbColor& color) {
    if (index < pixels_.size()) pixels_[index] = color;
  }

  // --- Test access ---------------------------------------------------------

  // What the last Show() latched. Tests should read this rather than the
  // working buffer, so they assert on what the strip would actually display.
  const std::vector<RgbColor>& shown() const { return shown_; }
  // How many times the strip was actually driven. Lets tests assert on the
  // refresh divider, which skips transfers rather than changing their content.
  uint32_t shownCount() const { return shown_count_; }
  uint8_t pin() const { return pin_; }

 private:
  std::vector<RgbColor> pixels_;
  std::vector<RgbColor> shown_;
  uint32_t shown_count_ = 0;
  uint8_t pin_;
};
