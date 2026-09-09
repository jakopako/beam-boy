#include "core/game_registry.h"

#include "scenes/network_scene.h"
#include "scenes/store_scene.h"
#include "scenes/wormfight_scene.h"

namespace beamboy {
namespace {

// Static instances rather than heap allocation: the whole set of games is known
// at build time, and a fixed layout keeps RAM usage predictable.
WormfightScene wormfight;
NetworkScene network;
StoreScene store;

}  // namespace

namespace games {

const GameEntry kGames[] = {
    // ids are storage keys for highscores -- never rename one.
    // Accent colours are spread deliberately around the hue wheel (not chosen
    // ad hoc) so adjacent launcher entries never share a colour family --
    // easy to do by accident with blue, since it is the "default" tech colour.
    {"wormfight", "Wormfight", Color(255, 40, 30), &wormfight, true},  // red
};

const uint8_t kGameCount = sizeof(kGames) / sizeof(kGames[0]);

// Always the last two launcher entries, in this order, after every built-in
// and installed game -- see GameList::build() in cartridge_store.cpp. Not
// games: is_game=false, so the engine leaves the nav button to the scene, and
// the radio stays off unless the user walks all the way here and asks for
// it -- opt-in is enforced by the fact that only Network/Store scenes can
// switch it on.
const GameEntry kUtilities[] = {
    {"store", "Store", Color(255, 80, 180), &store, false},      // pink
    {"network", "Network", Color(0, 255, 90), &network, false},  // green
};

const uint8_t kUtilityCount = sizeof(kUtilities) / sizeof(kUtilities[0]);

}  // namespace games
}  // namespace beamboy
