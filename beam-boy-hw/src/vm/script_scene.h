#pragma once

// Beam Boy — generic script cartridge host.
//
// One scene, reused for every scripted game: it owns a Berry VM, loads one
// script, and calls into it exactly the way Engine calls into a native Scene
// -- update(dt) then render() -- so porting a game from C++ to script is
// mechanical rather than a redesign. See docs/phase-6-cartridges.md.
//
// A script cartridge must define, at file scope:
//
//   def init()        -- optional; called once, from enter()
//   def update(dt)     -- game logic; dt is the fixed timestep in seconds
//   def render()        -- draw only, via beam.* calls
//
// The source comes from one of two places: a string literal baked into the
// firmware, or a `.be` file in the cartridge's folder on LittleFS. The second
// is what makes an installed game data rather than code -- see
// core/cartridge_store.h.
//
// Anything a script needs to persist across calls is its own global state --
// Berry has no per-scene instance here, matching how Wormfight/Reflex keep
// their own member fields today.
//
//   Nav button / B    handled by the engine (pause, exit) exactly as for a
//                      native game -- ScriptScene does not see them unless
//                      the game is a utility scene (see GameEntry::is_game).

#include "core/engine.h"
#include "vm/beam_api.h"

namespace beamboy {

class ScriptScene : public Scene {
 public:
  // A script host with nothing loaded yet, for cartridges discovered at
  // runtime -- setScriptPath() supplies the source before it is ever entered.
  ScriptScene() = default;

  // source is a pointer to a Berry script's text, held for the scene's
  // lifetime. Used for scripts baked into firmware as string literals.
  explicit ScriptScene(const char* source) : source_(source) {}

  // Points this scene at a script file on the filesystem. The source is read
  // in enter() and freed in exit(), rather than held resident: an installed
  // game that is not being played should cost metadata only, so the number of
  // installed cartridges is limited by flash, not RAM.
  void setScriptPath(const char* path);

  void enter(Engine& engine) override;
  void exit(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

  uint32_t score() const override { return ctx_.score; }

 private:
  // Releases the heap buffer if the source was loaded from a file. Never
  // touches a baked-in literal.
  void releaseSource();

  const char* source_ = nullptr;
  // Set when source_ points at a buffer this scene allocated and must free.
  // Distinguishing this from a string literal is what lets one class serve
  // both cases without a caller having to remember which kind it holds.
  bool owns_source_ = false;
  char script_path_[48] = {0};

  bvm* vm_ = nullptr;
  bool vm_ok_ = false;
  BeamApiContext ctx_;

  // Reports a script load/runtime error to Serial and leaves the scene
  // showing a blank screen rather than crashing -- a broken cartridge must
  // not take the console down with it (the full version of this guarantee is
  // the Phase 6 sandboxing item; this is the "don't crash" half of it).
  void reportError(const char* stage);
};

}  // namespace beamboy
