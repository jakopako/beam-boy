#include "scenes/vm_bench_scene.h"

#include <math.h>

namespace beamboy {

constexpr uint16_t VmBenchScene::kSteps[4];

namespace {

constexpr float kMinSpeed = 0.05f;
constexpr float kMaxSpeed = 0.45f;

// Per-entity variant: one Berry call per entity, mirroring what a naive
// per-entity cartridge update looks like. See kScriptBatched below for the
// alternative that isolates call overhead from interpretation cost.
const char kScriptPerEntity[] = R"be(
def bench_update(pos, vel, dt)
  pos = pos + vel * dt
  if pos >= 1.0
    pos = 1.0
    vel = -vel
  elif pos <= 0.0
    pos = 0.0
    vel = -vel
  end
  return [pos, vel]
end
)be";

// Batched variant: one call per frame over the whole entity list, looping
// internally. `entities` is a persistent Berry list rebuilt from native data
// only when the entity count changes (beginStep), not every frame -- a
// cartridge that owns its own state would do the same. This isolates how much
// of the per-entity-call cost above is call/marshalling overhead versus the
// interpretation itself: the arithmetic per entity is identical in both
// scripts.
const char kScriptBatched[] = R"be(
def bench_update_all(entities, dt)
  var n = size(entities)
  var i = 0
  while i < n
    var e = entities[i]
    var pos = e[0] + e[1] * dt
    var vel = e[1]
    if pos >= 1.0
      pos = 1.0
      vel = -vel
    elif pos <= 0.0
      pos = 0.0
      vel = -vel
    end
    e[0] = pos
    e[1] = vel
    i += 1
  end
end
)be";

}  // namespace

void VmBenchScene::enter(Engine& engine) {
  step_ = 0;
  complete_ = false;
  vm_ok_ = false;

  vm_ = be_vm_new();
  const char* script = batched_ ? kScriptBatched : kScriptPerEntity;
  // be_loadstring leaves a closure on the stack; be_pcall(0) runs it, which
  // is all a top-level `def` needs to register the function as a global. A
  // failure here means the script itself is broken, not the workload -- stop
  // rather than benchmark a VM that isn't actually running the code.
  if (be_loadstring(vm_, script) == 0 && be_pcall(vm_, 0) == 0) {
    vm_ok_ = true;
  } else {
    Serial.println("[vmbench] FATAL: script failed to load, see error below");
    if (be_top(vm_) > 0) {
      const char* msg = be_tostring(vm_, -1);
      if (msg) Serial.println(msg);
    }
  }
  be_pop(vm_, be_top(vm_));

  beginStep(0);
  engine.display().clear();

  Serial.println();
  Serial.print("=== Phase 5 benchmark: BERRY scripted update (");
  Serial.print(batched_ ? "batched" : "per-entity");
  Serial.println(" calls) ===");
  Serial.print("Frame budget: ");
  Serial.print(kFrameBudgetUs);
  Serial.print(" us at 60 fps, on ");
  Serial.print(engine.display().pixelCount());
  Serial.println(" pixels");
  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes  <- includes the VM's own footprint");
  Serial.println();
  if (batched_) {
    Serial.println(
        "One script call per FRAME, looping over a persistent list --");
    Serial.println(
        "isolates interpretation cost from per-call crossing overhead.");
  } else {
    Serial.println(
        "One script call per ENTITY per frame -- the expensive shape a");
    Serial.println(
        "naive per-entity cartridge update would use. Compare against");
    Serial.println(
        "the batched run (tap B before starting) to see how much of the");
    Serial.println("cost is call overhead rather than interpretation.");
  }
  Serial.println();
  Serial.println(
      "entities,mean_us,update_us,draw_us,worst_us,pct_of_budget,naive_"
      "slowdown,update_headroom");
}

void VmBenchScene::exit(Engine& engine) {
  (void)engine;
  if (vm_) {
    be_vm_delete(vm_);
    vm_ = nullptr;
  }
}

void VmBenchScene::beginStep(uint8_t step) {
  step_ = step;
  entity_count_ = kSteps[step];
  if (entity_count_ > kMaxEntities) entity_count_ = kMaxEntities;

  frames_this_step_ = 0;
  total_us_ = 0;
  total_update_us_ = 0;
  total_draw_us_ = 0;
  worst_us_ = 0;

  // Identical seeding to BenchScene, so both runs measure identical entity
  // trajectories and the comparison isn't skewed by different starting data.
  for (uint16_t i = 0; i < entity_count_; i++) {
    const float t =
        entity_count_ > 1 ? static_cast<float>(i) / (entity_count_ - 1) : 0.0f;
    entities_[i].pos = t;
    entities_[i].velocity =
        (kMinSpeed + t * (kMaxSpeed - kMinSpeed)) * ((i % 2) ? 1.0f : -1.0f);
    entities_[i].color = Color::hsv(t, 1.0f, 1.0f);
  }

  // Batched mode keeps the entity data resident in a Berry list across
  // frames, rebuilt only when the count changes here -- matching how a real
  // cartridge would own its state, and keeping per-frame cost to the update
  // call itself rather than list construction.
  if (batched_ && vm_ok_) {
    be_newlist(vm_);
    for (uint16_t i = 0; i < entity_count_; i++) {
      be_newlist(vm_);
      be_pushreal(vm_, entities_[i].pos);
      be_data_push(vm_, -2);
      be_pop(vm_, 1);
      be_pushreal(vm_, entities_[i].velocity);
      be_data_push(vm_, -2);
      be_pop(vm_, 1);
      be_data_push(vm_, -2);
      be_pop(vm_, 1);
    }
    be_setglobal(vm_, "bench_entities");
    be_pop(vm_, 1);
  }
}

void VmBenchScene::runWorkload(Engine& engine) {
  Display& display = engine.display();
  const float dt = 1.0f / 60.0f;

  display.clear();

  const uint32_t update_start = micros();
  if (vm_ok_ && batched_) {
    // One call for the whole frame. The script mutates bench_entities in
    // place, so results are read back once per entity afterward -- that
    // readback is real cost too (it is what a cartridge would pay to get data
    // back for drawing) and is included in update_us, not draw_us.
    be_getglobal(vm_, "bench_update_all");
    be_getglobal(vm_, "bench_entities");
    be_pushreal(vm_, dt);
    if (be_pcall(vm_, 2) == 0) {
      be_getglobal(vm_, "bench_entities");
      for (uint16_t i = 0; i < entity_count_; i++) {
        Entity& e = entities_[i];
        be_pushint(vm_, i);
        be_getindex(vm_, -2);
        be_pushint(vm_, 0);
        be_getindex(vm_, -2);
        e.pos = be_toreal(vm_, -1);
        be_pop(vm_, 2);
        be_pushint(vm_, i);
        be_getindex(vm_, -2);
        be_pushint(vm_, 1);
        be_getindex(vm_, -2);
        e.velocity = be_toreal(vm_, -1);
        be_pop(vm_, 2);
      }
    }
    be_pop(vm_, be_top(vm_));
  } else if (vm_ok_) {
    // The scripted half: one Berry call per entity, each pushing two floats in
    // and getting a two-element list back. This is deliberately the expensive
    // shape -- a real cartridge calls into script once per entity, not once for
    // the whole frame with a script-resident list -- because per-call overhead
    // (stack setup, argument marshalling, GC bookkeeping) is exactly what
    // native code never pays and what update_headroom needs to capture.
    for (uint16_t i = 0; i < entity_count_; i++) {
      Entity& e = entities_[i];

      be_getglobal(vm_, "bench_update");
      be_pushreal(vm_, e.pos);
      be_pushreal(vm_, e.velocity);
      be_pushreal(vm_, dt);
      if (be_pcall(vm_, 3) == 0) {
        // Return value is a two-element list: [pos, vel]. be_getindex reads
        // the subscript already on top of the stack, so push it before each
        // call -- unlike a native array access, this is itself part of the
        // measured per-call overhead.
        be_pushint(vm_, 0);
        be_getindex(vm_, -2);
        e.pos = be_toreal(vm_, -1);
        be_pop(vm_, 1);
        be_pushint(vm_, 1);
        be_getindex(vm_, -2);
        e.velocity = be_toreal(vm_, -1);
        be_pop(vm_, 1);
      }
      be_pop(vm_, be_top(vm_));
    }
  }
  update_us_ = micros() - update_start;

  const uint32_t draw_start = micros();
  for (uint16_t i = 0; i < entity_count_; i++) {
    display.point(entities_[i].pos, entities_[i].color, 0.6f);
  }
  draw_us_ = micros() - draw_start;
}

void VmBenchScene::update(Engine& engine, float dt) {
  (void)dt;

  if (complete_) {
    if (engine.input().pressed(Button::kA)) {
      enter(engine);
    }
    if (engine.input().pressed(Button::kB)) {
      // Toggle calling convention before the next run, so both numbers can be
      // captured from one flash without reflashing between them.
      batched_ = !batched_;
      enter(engine);
    }
    return;
  }

  const uint32_t start = micros();
  runWorkload(engine);
  last_us_ = micros() - start;

  total_us_ += last_us_;
  total_update_us_ += update_us_;
  total_draw_us_ += draw_us_;
  if (last_us_ > worst_us_) worst_us_ = last_us_;
  frames_this_step_++;

  if (frames_this_step_ >= kFramesPerStep) {
    reportStep(engine);

    if (step_ + 1 < kStepCount) {
      beginStep(step_ + 1);
    } else {
      complete_ = true;
      Serial.println("=== benchmark complete (A to re-run) ===");
      Serial.println();
    }
  }
}

void VmBenchScene::reportStep(Engine& engine) {
  (void)engine;

  const uint32_t mean =
      frames_this_step_ > 0 ? total_us_ / frames_this_step_ : 0;
  const float pct = mean * 100.0f / kFrameBudgetUs;

  const float affordable =
      mean > 0 ? static_cast<float>(kFrameBudgetUs) / mean : 0.0f;

  const uint32_t mean_update =
      frames_this_step_ > 0 ? total_update_us_ / frames_this_step_ : 0;
  const uint32_t mean_draw =
      frames_this_step_ > 0 ? total_draw_us_ / frames_this_step_ : 0;

  float update_headroom = 0.0f;
  if (mean_update > 0 && mean_draw < kFrameBudgetUs) {
    update_headroom = static_cast<float>(kFrameBudgetUs - mean_draw) /
                      static_cast<float>(mean_update);
  }

  Serial.print(entity_count_);
  Serial.print(",");
  Serial.print(mean);
  Serial.print(",");
  Serial.print(mean_update);
  Serial.print(",");
  Serial.print(mean_draw);
  Serial.print(",");
  Serial.print(worst_us_);
  Serial.print(",");
  Serial.print(pct, 2);
  Serial.print(",");
  Serial.print(affordable, 1);
  Serial.print(",");
  Serial.println(update_headroom, 1);
}

void VmBenchScene::render(Engine& engine) {
  Display& display = engine.display();

  if (complete_) {
    const float level = 0.3f + 0.3f * pulse(millis() / 1000.0f, 2.0f);
    display.rawPixel(0, colors::kBlue.scaled(level));
    display.rawPixel(display.pixelCount() - 1, colors::kBlue.scaled(level));
    return;
  }

  const float progress = static_cast<float>(frames_this_step_) / kFramesPerStep;
  display.rawPixel(0, colors::kAmber.scaled(0.15f + 0.6f * progress));
}

}  // namespace beamboy
