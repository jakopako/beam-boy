#pragma once

#include <stdint.h>

namespace beamboy {

// The physical launcher contract: nine game positions followed by one
// permanently anchored Settings position.
constexpr uint8_t kMaxGames = 9;
constexpr uint8_t kLauncherSlotCount = 10;
constexpr uint8_t kSettingsSlot = kLauncherSlotCount - 1;

static_assert(kMaxGames + 1 == kLauncherSlotCount,
              "launcher slots must be nine games plus Settings");

// New games consume a launcher slot; updates do not.
constexpr bool canInstallGame(uint8_t game_count, bool already_installed) {
  return already_installed || game_count < kMaxGames;
}

constexpr uint8_t maxInstalledGames(uint8_t built_in_count) {
  return built_in_count < kMaxGames ? kMaxGames - built_in_count : 0;
}

// Launcher selection is dense (games followed immediately by Settings), while
// its physical representation deliberately is not: Settings stays at slot 9
// and any positions between it and the last game remain dark.
constexpr uint8_t launcherSlotForSelection(uint8_t selection,
                                           uint8_t game_count) {
  return selection < game_count ? selection : kSettingsSlot;
}

inline uint8_t moveLauncherSelection(uint8_t selection, uint8_t game_count,
                                     int8_t step) {
  const int16_t next = static_cast<int16_t>(selection) + step;
  if (next < 0) return 0;
  if (next > game_count) return game_count;
  return static_cast<uint8_t>(next);
}

inline uint8_t reconcileLauncherSelection(uint8_t selection,
                                          bool settings_selected,
                                          uint8_t game_count) {
  if (settings_selected) return game_count;
  if (game_count == 0) return 0;
  return selection < game_count ? selection : game_count - 1;
}

constexpr uint16_t launcherSlotStart(uint8_t slot, uint16_t pixel_count) {
  return static_cast<uint16_t>(
      (static_cast<uint32_t>(slot) * pixel_count) / kLauncherSlotCount);
}

constexpr uint16_t launcherSlotEnd(uint8_t slot, uint16_t pixel_count) {
  return static_cast<uint16_t>(
      (static_cast<uint32_t>(slot + 1) * pixel_count) / kLauncherSlotCount);
}

}  // namespace beamboy
