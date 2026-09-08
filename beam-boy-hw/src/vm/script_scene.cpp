#include "vm/script_scene.h"

#include <string.h>

#include "core/cartridge_store.h"

namespace beamboy {

void ScriptScene::setScriptPath(const char* path) {
  strncpy(script_path_, path, sizeof(script_path_) - 1);
  script_path_[sizeof(script_path_) - 1] = '\0';
}

void ScriptScene::releaseSource() {
  if (owns_source_ && source_ != nullptr) {
    free(const_cast<char*>(source_));
  }
  // Only clear the pointer for a buffer we owned. A baked-in literal must
  // survive, since the same scene instance is entered again on every replay.
  if (owns_source_) {
    source_ = nullptr;
    owns_source_ = false;
  }
}

void ScriptScene::reportError(const char* stage) {
  Serial.print("[script] error during ");
  Serial.println(stage);
  if (vm_ && be_top(vm_) > 0) {
    const char* msg = be_tostring(vm_, -1);
    if (msg) Serial.println(msg);
  }
  vm_ok_ = false;
}

void ScriptScene::enter(Engine& engine) {
  ctx_.engine = &engine;
  ctx_.score = 0;
  ctx_.exit_requested = false;
  vm_ok_ = false;

  // A filesystem cartridge is read fresh on every entry. That costs a file
  // read per launch, and buys two things: RAM proportional to what is being
  // played rather than to what is installed, and a game that picks up an
  // updated file without a reboot.
  if (script_path_[0] != '\0') {
    releaseSource();
    source_ = CartridgeStore::readScript(script_path_);
    owns_source_ = source_ != nullptr;
    if (source_ == nullptr) {
      Serial.print("[script] cannot read ");
      Serial.println(script_path_);
      engine.display().clear();
      return;
    }
  }

  if (source_ == nullptr) {
    engine.display().clear();
    return;
  }

  vm_ = be_vm_new();
  bindBeamApi(vm_);
  setBeamApiContext(&ctx_);

  // Load and run the script's top level in one step: a well-formed cartridge
  // only *defines* init/update/render at this point, so running it should
  // never do visible work -- but be_pcall runs it regardless, matching how
  // VmBenchScene registers its functions (see vm_bench_scene.cpp).
  if (be_loadstring(vm_, source_) != 0 || be_pcall(vm_, 0) != 0) {
    reportError("script load");
    be_pop(vm_, be_top(vm_));
    engine.display().clear();
    return;
  }
  be_pop(vm_, be_top(vm_));

  if (be_getglobal(vm_, "init")) {
    if (be_pcall(vm_, 0) != 0) {
      reportError("init()");
      be_pop(vm_, be_top(vm_));
      engine.display().clear();
      return;
    }
  }
  be_pop(vm_, be_top(vm_));

  vm_ok_ = true;
  engine.display().clear();
}

void ScriptScene::exit(Engine& engine) {
  (void)engine;
  if (vm_) {
    be_vm_delete(vm_);
    vm_ = nullptr;
  }
  // Freed only after the VM is gone: Berry's compiled bytecode does not point
  // back into the source text, but destroying the VM first keeps the ordering
  // obviously correct rather than merely true today.
  releaseSource();
  setBeamApiContext(nullptr);
}

void ScriptScene::update(Engine& engine, float dt) {
  if (!vm_ok_) return;

  setBeamApiContext(&ctx_);

  if (be_getglobal(vm_, "update")) {
    be_pushreal(vm_, dt);
    if (be_pcall(vm_, 1) != 0) {
      reportError("update()");
    }
  }
  be_pop(vm_, be_top(vm_));

  if (ctx_.exit_requested) {
    engine.exitToLauncher();
  }
}

void ScriptScene::render(Engine& engine) {
  if (!vm_ok_) return;

  setBeamApiContext(&ctx_);

  if (be_getglobal(vm_, "render")) {
    if (be_pcall(vm_, 0) != 0) {
      reportError("render()");
    }
  }
  be_pop(vm_, be_top(vm_));
}

}  // namespace beamboy
