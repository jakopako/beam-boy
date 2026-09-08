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

}  // namespace beamboy
