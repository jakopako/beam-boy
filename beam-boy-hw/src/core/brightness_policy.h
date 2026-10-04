#pragma once

#include <stdint.h>

#include "board_config.h"

namespace beamboy {
namespace brightness {

static_assert(board::kBrightnessCap > 0, "brightness cap must be nonzero");
constexpr uint8_t kDefault = board::kBrightnessDefault;
static_assert(kDefault > 0 && kDefault <= board::kBrightnessCap,
              "default brightness must be within the board cap");
constexpr uint8_t kMinimum =
    kDefault >= 8 ? kDefault / 8 : 1;
constexpr uint8_t kStep =
    kDefault >= 16 ? kDefault / 16 : 1;

constexpr uint8_t clamp(int value) {
  return value < kMinimum
             ? kMinimum
             : (value > board::kBrightnessCap ? board::kBrightnessCap
                                             : static_cast<uint8_t>(value));
}

// Zero is the save format's "use the board default" sentinel, not lights off.
constexpr uint8_t fromStored(uint8_t value) {
  return value == 0 ? kDefault : clamp(value);
}

constexpr uint8_t stepped(uint8_t value, int8_t direction) {
  return clamp(static_cast<int>(value) +
               static_cast<int>(direction) * kStep);
}

}  // namespace brightness
}  // namespace beamboy
