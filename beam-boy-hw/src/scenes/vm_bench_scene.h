#pragma once

// Beam Boy — Phase 5 VM bake-off: the Berry half.
//
// Mirrors BenchScene exactly, but the update half of the workload -- moving
// each entity and bouncing it off the ends -- is run as a Berry script rather
// than native C++. The draw half stays native either way (a script calls
// beam.point() and the pixel maths inside is still compiled C++), which is
// the whole reason update_headroom, not naive_slowdown, is the number that
// matters. See docs/phase-5-vm-bakeoff.md.
//
// runWorkload() here is the one function that differs from BenchScene: it
// calls into a Berry closure instead of a native loop. Everything else --
// sweep steps, sample window, CSV shape -- is identical on purpose, so the
// two runs are directly comparable.
//
// Two calling conventions are measured, selectable at compile time via
// kBatched (see vm_bench_scene.cpp): one Berry call per entity per frame
// (worst case, what a naive per-entity update looks like) versus one call per
// frame over a persistent script-side list (what a cartridge that keeps its
// own entity list would do). Comparing the two isolates how much of the
// per-entity cost is call overhead versus actual interpretation.
//
//   A     step to the next entity count
//   B     hold to freeze the display so results can be read

#include "core/engine.h"

extern "C" {
#include "berry.h"
}

namespace beamboy {

class VmBenchScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void exit(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

  bool complete() const { return complete_; }

 private:
  static constexpr uint16_t kMaxEntities = 100;
  static constexpr uint16_t kSteps[4] = {10, 25, 50, 100};
  static constexpr uint8_t kStepCount = 4;
  static constexpr uint16_t kFramesPerStep = 180;  // 3 seconds at 60 fps
  static constexpr uint32_t kFrameBudgetUs = 16667;

  void beginStep(uint8_t step);
  void reportStep(Engine& engine);
  void runWorkload(Engine& engine);

  // Native-side entity storage. The script only ever sees a Berry list built
  // fresh from this each call -- see the comment in vm_bench_scene.cpp on why
  // that copy, and not a persistent script-side list, is what is measured.
  struct Entity {
    float pos = 0.0f;
    float velocity = 0.0f;
    Color color;
  };
  Entity entities_[kMaxEntities];
  uint16_t entity_count_ = 0;

  bvm* vm_ = nullptr;
  Engine* active_engine_ =
      nullptr;  // Set only for the duration of a workload call, so the bound
                // beam.point() can reach the display without a global.

  uint8_t step_ = 0;
  uint16_t frames_this_step_ = 0;
  uint32_t total_us_ = 0;
  uint32_t total_update_us_ = 0;
  uint32_t total_draw_us_ = 0;
  uint32_t worst_us_ = 0;
  uint32_t last_us_ = 0;
  uint32_t update_us_ = 0;
  uint32_t draw_us_ = 0;

  bool complete_ = false;
  bool vm_ok_ = false;
  // Toggled by B on the completion screen (see update()), so both calling
  // conventions can be measured from one flash without reflashing between.
  bool batched_ = false;
};

}  // namespace beamboy
