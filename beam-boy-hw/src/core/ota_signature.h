#pragma once

// Beam Boy — release-manifest signature verification.
//
// The updater trusts a manifest only if it is signed by a key compiled into
// this firmware: ECDSA P-256 over SHA-256 of the exact decoded payload bytes,
// signature as raw r||s. This is updater-level authenticity -- independent of
// TLS and of who hosts the files -- not ESP32 Secure Boot; no eFuses are
// involved.
//
// Private keys never touch the repository or a device. They are generated and
// used offline with tools/firmware/fwrelease.py, which prints the public-key
// entry to paste into ota_keys.cpp.

#include <stddef.h>
#include <stdint.h>

#include "core/fw_manifest.h"

namespace beamboy {
namespace fw {

// SEC1 uncompressed point: 0x04 || X || Y.
constexpr size_t kPublicKeyBytes = 65;

struct TrustedKey {
  const char* id;
  uint8_t public_key[kPublicKeyBytes];
};

// The key with this id from ota_keys.cpp, or null. Looking up by id lets a
// rotation release trust old and new keys at once.
const TrustedKey* findTrustedKey(const char* key_id);

enum class SignatureResult : uint8_t {
  kOk,
  kUnknownKey,
  kBadKey,
  kBadSignature,
  kCryptoError,
};

const char* describe(SignatureResult result);

// Device-only (mbedTLS). Verifies `signature` over SHA-256(payload).
SignatureResult verifySignature(const uint8_t* payload, size_t length,
                                const uint8_t* signature,
                                const uint8_t* public_key);

// Convenience: looks the envelope's key up, then verifies the decoded payload.
SignatureResult verifyEnvelope(const Envelope& envelope,
                               const uint8_t* payload, size_t length);

}  // namespace fw
}  // namespace beamboy
