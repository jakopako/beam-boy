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
// Never commit a test or development key here; device test vectors live under
// test/test_device_ota_signature/.

#include "core/ota_signature.h"

namespace beamboy {
namespace fw {

extern const TrustedKey kTrustedKeys[] = {
    // Release key 1, created 2026-10-16. Private half held offline by the
    // maintainer.
    {"bb-release-1",
     {
         0x04, 0xef, 0xf4, 0x0b, 0x9c, 0x6d, 0x38, 0xd9, 0x5b, 0x13, 0xf3, 0x37,
         0xbf, 0xf1, 0x9b, 0x93, 0x84, 0xe5, 0x19, 0x9f, 0x06, 0x43, 0xa5, 0x1a,
         0x6d, 0xe7, 0x74, 0x87, 0x3e, 0x72, 0x40, 0x31, 0x90, 0x85, 0x8f, 0xd3,
         0x6b, 0xda, 0x6e, 0x33, 0x03, 0xa9, 0x56, 0xb1, 0xd1, 0x21, 0x51, 0xc7,
         0xa2, 0x5a, 0x61, 0x5c, 0xcb, 0xfd, 0xea, 0x2e, 0x8c, 0x9d, 0x12, 0xcf,
         0x8e, 0x40, 0x64, 0x73, 0x1b,
     }},
    {nullptr, {0}},  // Terminator; must stay last.
};

}  // namespace fw
}  // namespace beamboy
