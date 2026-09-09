#pragma once

// Beam Boy — the `beam` module bound into every script cartridge's Berry VM.
//
// This is the entire surface a downloadable game can see. Scripts never touch
// Engine/Display/Input/Storage directly -- everything a cartridge can do goes
// through one of these functions, which is what makes sandboxing later
// (Phase 6 item 4) a matter of policing this file rather than auditing every
// cartridge that ever ships.
//
// Naming and shape follow the sketch in PLAN.md's cartridge API section
// exactly, so there is one API description, not two that can drift apart.

extern "C" {
#include "berry.h"
}

#include "core/engine.h"

namespace beamboy {

// Per-VM state the bound functions need but that isn't available from `bvm*`
// alone: which Engine to draw into, and the score a script has accumulated so
// far. Set as a thread-unsafe global by design -- Beam Boy runs exactly one
// script VM at a time, never concurrently or re-entrantly, so a single active
// context is sufficient and far cheaper than threading a userdata pointer
// through every native call.
struct BeamApiContext {
  Engine* engine = nullptr;
  uint32_t score = 0;
  bool exit_requested = false;
};

// Points every bound native function at ctx until the next call to this
// function. Must be called before any be_pcall() that might run script code
// touching the beam module -- ScriptScene does this once per frame.
void setBeamApiContext(BeamApiContext* ctx);
BeamApiContext* beamApiContext();

// Creates the `beam` global module in vm and binds every native function to
// it. Call once, right after creating vm.
void bindBeamApi(bvm* vm);

// Installs the sandbox: a per-call time budget and a VM memory ceiling that
// abort a runaway script rather than let it hang the console or exhaust RAM.
// Cartridges can come from the filesystem and, eventually, the internet --
// this is what makes a buggy or hostile one a caught exception instead of a
// frozen device. Call once, right after bindBeamApi().
//
// Implemented on top of Berry's observability hook (be_set_obs_hook), which
// fires periodically from inside the interpreter loop (see
// BE_VM_OBSERVABILITY_SAMPLING in generate/berry_conf.h -- tightened from the
// upstream default so the time check actually fires often enough to catch an
// overrun before it costs multiple frames) -- no changes to the vendored
// Berry source are needed.
void installSandbox(bvm* vm);

// Marks the start of one guarded call (update() or render()) so the sandbox's
// time budget is measured per call, not cumulatively across a scene's whole
// lifetime. Call immediately before each be_pcall() that may run script code.
void beginSandboxedCall();

}  // namespace beamboy
