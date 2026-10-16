#pragma once

// Beam Boy — how the firmware-update screen looks, as a pure function of the
// flow's state so the host tests can check every frame.
//
// Each stage differs in shape and motion, not colour alone (the tube is also
// read by people who cannot tell red from green):
//
//   no credentials  two amber bars, double pulse
//   connecting      blue comet sweeping the tube
//   checking        dim steady blue (painted before the blocking check)
//   up to date      steady blue after one white acknowledgement sweep
//   available       steady turquoise; holding A fills white from the start
//   downloading     blue bar filled to the real verified byte count
//   verifying       white points sweeping inward from both ends
//   installed       green fills from both ends, then a white centre heartbeat
//   failed          red, with a rhythm per reason:
//                     connect 1 pulse, check 2, incompatible 3, install 4,
//                     power: slow breathing instead of pulses

#include <stdint.h>

#include "core/display.h"
#include "scenes/update_flow.h"

namespace beamboy {
namespace update {

struct View {
  Stage stage;
  Failure failure;
  uint32_t stage_ms;
  float download_progress;  // 0..1, kDownloading only.
  float confirm_progress;   // 0..1, kAvailable only.
};

void render(Display& display, const View& view);

// Rhythm helpers, exposed for tests.
constexpr uint32_t kPulseOnMs = 180;
constexpr uint32_t kPulseGapMs = 180;
constexpr uint32_t kPulseCycleMs = 2400;
constexpr uint32_t kSuccessFillMs = 1000;
constexpr uint32_t kAcknowledgeMs = 700;

// True during one of `count` evenly spaced pulses at the start of each cycle.
bool pulseOn(uint32_t elapsed_ms, uint8_t count, uint32_t cycle_ms);

// Number of pulses in a failure's rhythm; 0 means it breathes instead.
uint8_t failurePulses(Failure failure);

}  // namespace update
}  // namespace beamboy
