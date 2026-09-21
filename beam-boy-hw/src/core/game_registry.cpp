#include "core/game_registry.h"

#include "scenes/network_scene.h"
#include "scenes/settings_scene.h"
#include "scenes/store_scene.h"
#include "scenes/wormfight_scene.h"

namespace beamboy {
namespace {

// Static instances rather than heap allocation: the whole set of games is known
// at build time, and a fixed layout keeps RAM usage predictable.
WormfightScene wormfight;
NetworkScene network;
StoreScene store;
SettingsScene settings;

}  // namespace

namespace games {

const GameEntry kGames[] = {
    // ids are storage keys for highscores -- never rename one.
    // Accent colours are spread deliberately around the hue wheel (not chosen
    // ad hoc) so adjacent launcher entries never share a colour family --
    // easy to do by accident with blue, since it is the "default" tech colour.
    {"wormfight", "Wormfight", Color(255, 40, 30), &wormfight, true,
     false},  // red
};

const uint8_t kGameCount = sizeof(kGames) / sizeof(kGames[0]);
static_assert(kGameCount <= kMaxGames,
              "built-in games exceed the nine-game launcher capacity");

// Utilities remain addressable by GameList so the engine can provide its
// common hold-B return gesture. Only Settings is rendered by the launcher;
// Store and Network are reached from there.
const GameEntry kUtilities[] = {
    {"store", "Store", Color(255, 80, 180), &store, false,
     false},  // pink
    {"network", "Network", Color(0, 255, 90), &network, false,
     false},  // green
    {"settings", "Settings", Color(150, 110, 255), &settings, false,
     false},  // violet
};

static_assert(sizeof(kUtilities) / sizeof(kUtilities[0]) == kUtilityCount,
              "utility index constants must match kUtilities");

}  // namespace games
}  // namespace beamboy
