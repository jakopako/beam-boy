#include "core/game_registry.h"

#include "scenes/brightness_scene.h"
#include "scenes/network_scene.h"
#include "scenes/settings_scene.h"
#include "scenes/store_scene.h"
#include "scenes/update_firmware_scene.h"

namespace beamboy {
namespace {

// Static instances rather than heap allocation keep utility scene storage
// predictable.
NetworkScene network;
StoreScene store;
BrightnessScene brightness;
SettingsScene settings;
UpdateFirmwareScene update_firmware;

}  // namespace

namespace games {

// Utilities remain addressable by GameList so the engine can provide its
// common hold-B return gesture. Only Settings is rendered by the launcher;
// All four child utilities are reached from there.
const GameEntry kUtilities[] = {
    {"store", "Store", Color(255, 80, 180), &store, false, false},      // pink
    {"network", "WiFi", Color(0, 255, 90), &network, false, false},     // green
    {"brightness", "Brightness", Color(255, 255, 255), &brightness, false,
     false},  // white
    {"settings", "Settings", Color(150, 110, 255), &settings, false,
     false},  // violet
    {"update", "Update firmware", Color(255, 140, 0), &update_firmware, false,
     false},  // amber, unavailable placeholder
};

static_assert(sizeof(kUtilities) / sizeof(kUtilities[0]) == kUtilityCount,
              "utility index constants must match kUtilities");

}  // namespace games
}  // namespace beamboy
