#pragma once

#include <stdint.h>

// The parts of the network policy that are pure decisions, kept deliberately
// free of any WiFi headers.
//
// This split exists so the rules can be tested on the host. network.h drags in
// the whole WiFi stack, which cannot be compiled natively without shimming a
// large and rapidly-moving driver API -- but the interesting part is not the
// driver, it is the question "given this outcome, do we keep the credentials?".
// That question is a two-input truth table, and getting it wrong either strands
// the user with an unusable Connect entry or silently erases a working
// configuration. It deserves tests; the driver call around it does not.

namespace beamboy {

// Why a connection attempt ended.
enum class FailReason : uint8_t {
  kNone,
  kBadPassword,  // The AP rejected our key. Retrying will not help.
  kNotFound,     // No AP with that name was heard. Out of range, or a typo.
  kTimeout,      // Associated with nothing conclusive before the deadline.
  kLost,         // Was connected, then dropped.
};

// Whether a failure should cause the stored credentials to be thrown away.
//
// Both conditions have to hold, and each rules out a different kind of mistake:
//
//   reason == kBadPassword
//       Only a definite rejection by the AP counts. kNotFound is deliberately
//       excluded -- a mistyped SSID is indistinguishable from being out of
//       range, and being away from home is far more common than a typo, so
//       discarding there would punish the wrong user. kTimeout says nothing at
//       all. kLost cannot indicate bad credentials, since they had just worked.
//
//   unproven
//       The credentials have never produced a connection. Once a network has
//       worked even once, a later rejection is far more likely to be a flaky
//       AP or a password the user changed on the router than something this
//       device should act on unilaterally; Forget stays the deliberate way out.
//
// Keeping this as a free function -- rather than reading member state inside
// failWith() -- is what makes the table above enumerable in a test.
constexpr bool shouldDiscardCredentials(FailReason reason, bool unproven) {
  return reason == FailReason::kBadPassword && unproven;
}

}  // namespace beamboy
