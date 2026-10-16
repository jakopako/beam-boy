// Beam Boy — public keys this firmware accepts release manifests from.
//
// Only public keys belong here. Generate a release key offline with
//
//   python tools/firmware/fwrelease.py keygen --key-id bb-release-1 --out <offline path>
//
// which prints the entry to paste below. Keep the private key off GitHub and
// off every device. To rotate, ship a release that trusts both the old and new
// ids, sign the next releases with the new key, and only then drop the old one.
//
// No release key has been approved yet (see PLAN.md Phase 11, "Remaining design
// gate"), so the table is empty and every manifest is refused with "untrusted
// key". Never commit a test or development key here; device test vectors live
// under test/test_device_ota_signature/.

#include "core/ota_signature.h"

namespace beamboy {
namespace fw {

extern const TrustedKey kTrustedKeys[] = {
    // {"bb-release-1", {0x04, ...}},
    {nullptr, {0}},  // Terminator; must stay last.
};

}  // namespace fw
}  // namespace beamboy
