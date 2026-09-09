#include "core/game_registry.h"

#include "scenes/bench_scene.h"
#include "scenes/network_scene.h"
#include "scenes/reflex_scene.h"
#include "scenes/reflex_script.h"
#include "scenes/store_scene.h"
#include "scenes/vm_bench_scene.h"
#include "scenes/wormfight_scene.h"
#include "vm/script_scene.h"

namespace beamboy {
namespace {

// Static instances rather than heap allocation: the whole set of games is known
// at build time, and a fixed layout keeps RAM usage predictable. When
// cartridges become scripts, these are replaced by a single script host scene
// reused for every game.
WormfightScene wormfight;
ReflexScene reflex;
ScriptScene reflex_script(kReflexScript);
BenchScene bench;
VmBenchScene vm_bench;
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
    {"reflex", "Reflex", Color(0, 180, 255), &reflex, true},           // cyan
    // The Phase 6 cartridge model: the same game, but running through Berry
    // via ScriptScene instead of native C++. A separate registry entry (and
    // storage id) rather than replacing "reflex" outright, so the two can be
    // played side by side and compared -- see docs/phase-6-cartridges.md.
    {"reflexvm", "Reflex (VM)", Color(255, 200, 0), &reflex_script,
     true},  // amber/gold
    // Not a game: the Phase 5 VM benchmark. Lives in the launcher so it can be
    // run on real hardware without reflashing, and is deliberately last.
    // is_game=false, so the engine leaves the nav button to the scene.
    {"bench", "Benchmark", Color(255, 255, 255), &bench, false},  // white
    // Same benchmark, scripted update half -- the Berry side of the bake-off.
    // Compare its CSV against "bench" above; see docs/phase-5-vm-bakeoff.md.
    {"vmbench", "VM Bench", Color(160, 0, 255), &vm_bench, false},  // violet
    // Also not a game. Near the end, because the radio stays off unless the
    // user walks all the way here and asks for it -- opt-in is enforced by the
    // fact that only Network/Store scenes can switch it on.
    {"network", "Network", Color(0, 255, 90), &network, false},  // green
    // Downloads script cartridges from the configured Phase 7 store. Utility
    // scene rather than a game: entering it is the opt-in to turn WiFi on.
    {"store", "Store", Color(255, 80, 180), &store, false},  // pink
};

const uint8_t kGameCount = sizeof(kGames) / sizeof(kGames[0]);

}  // namespace games
}  // namespace beamboy
