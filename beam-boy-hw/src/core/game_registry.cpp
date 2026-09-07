#include "core/game_registry.h"

#include "scenes/bench_scene.h"
#include "scenes/network_scene.h"
#include "scenes/reflex_scene.h"
#include "scenes/wormfight_scene.h"

namespace beamboy {
namespace {

// Static instances rather than heap allocation: the whole set of games is known
// at build time, and a fixed layout keeps RAM usage predictable on the ESP8266.
// When cartridges become scripts, these are replaced by a single script host
// scene reused for every game.
WormfightScene wormfight;
ReflexScene reflex;
BenchScene bench;
NetworkScene network;

}  // namespace

namespace games {

const GameEntry kGames[] = {
    // ids are storage keys for highscores -- never rename one.
    {"wormfight", "Wormfight", Color(255, 40, 30), &wormfight, true},
    {"reflex", "Reflex", Color(0, 180, 255), &reflex, true},
    // Not a game: the Phase 5 VM benchmark. Lives in the launcher so it can be
    // run on real hardware without reflashing, and is deliberately last.
    // is_game=false, so the engine leaves the nav button to the scene.
    {"bench", "Benchmark", Color(255, 255, 255), &bench, false},
    // Also not a game. Last, because the radio stays off unless the user walks
    // all the way here and asks for it -- opt-in is enforced by the fact that
    // this is the only scene that can switch it on.
    {"network", "Network", Color(0, 160, 255), &network, false},
};

const uint8_t kGameCount = sizeof(kGames) / sizeof(kGames[0]);

}  // namespace games
}  // namespace beamboy
