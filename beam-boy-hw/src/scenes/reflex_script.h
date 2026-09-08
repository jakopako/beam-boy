#pragma once

// Beam Boy — the Reflex cartridge, embedded as source.
//
// This is the *built-in* copy, baked into the firmware as a C string so there
// is always a scripted game present even on a console with an empty
// filesystem. Cartridges proper live in /games/ on LittleFS and are loaded by
// core/cartridge_store.h -- see beam-boy-hw/data/README.md.

namespace beamboy {

extern const char kReflexScript[];

}  // namespace beamboy
