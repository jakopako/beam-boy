#pragma once

#include "core/game_registry.h"

namespace beamboy {
namespace settings {

constexpr uint8_t kItems[] = {
    games::kNetworkUtilityIndex,
    games::kUpdateFirmwareUtilityIndex,
    games::kStoreUtilityIndex,
    games::kBrightnessUtilityIndex,
};
constexpr uint8_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);
static_assert(kItemCount == 4, "Settings must have exactly four entries");

}  // namespace settings
}  // namespace beamboy
