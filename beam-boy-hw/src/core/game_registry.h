#pragma once

// Beam Boy — game registry.
//
// The launcher needs to show a list of games without knowing what any of them
// are. This is that list.
//
// Deliberately shaped like the `meta.json` that downloadable cartridges carry:
// an id, a title, an accent colour. Games installed on the filesystem are read
// into this same struct by core/cartridge_store.h and appended to this list,
// so the launcher sees one uniform set of entries and does not know or care
// which of them came from flash.
//
// The `id` is the storage key for highscores and must stay stable forever --
// renaming it silently orphans everyone's scores.

#include <Arduino.h>

#include "display.h"

namespace beamboy {

class Scene;

struct GameEntry {
  const char* id;
  const char* title;
  Color accent;
  Scene* scene;

  // False for entries that are not games: the Network scene, the benchmark, and
  // anything else that lives in the launcher for convenience.
  //
  // The engine uses this to decide whether to apply the pause/exit gesture. A
  // game must not be able to trap the player, so the engine intercepts the nav
  // button *before* the scene sees it -- but a utility scene needs that button
  // for its own menu, and would otherwise be unusable. Utility scenes are
  // responsible for their own exit instead.
  //
  // Deliberately has no default initializer: that would make GameEntry a
  // non-aggregate under C++11, which the ESP32 Arduino core still compiles
  // with, and the brace-initialised table below would stop compiling. Every
  // entry sets the flag explicitly, so a default would be unused anyway.
  bool is_game;
};

namespace games {

// All games built into the firmware. These come first in the launcher; games
// installed on the filesystem are appended after them (see cartridge_store.h),
// so installing or deleting a cartridge never renumbers a built-in.
extern const GameEntry kGames[];
extern const uint8_t kGameCount;

}  // namespace games
}  // namespace beamboy
