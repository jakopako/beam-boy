#include "core/ota_signature.h"

#include <mbedtls/bignum.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace beamboy {
namespace fw {

extern const TrustedKey kTrustedKeys[];

const TrustedKey* findTrustedKey(const char* key_id) {
  if (key_id == nullptr || key_id[0] == '\0') return nullptr;
  for (const TrustedKey* key = kTrustedKeys; key->id != nullptr; key++) {
    if (strcmp(key->id, key_id) == 0) return key;
  }
  return nullptr;
}

const char* describe(SignatureResult result) {
  switch (result) {
    case SignatureResult::kOk:
      return "signature ok";
    case SignatureResult::kUnknownKey:
      return "manifest signed by an untrusted key";
    case SignatureResult::kBadKey:
      return "trusted key is malformed";
    case SignatureResult::kBadSignature:
      return "manifest signature invalid";
    case SignatureResult::kCryptoError:
      return "signature check failed to run";
  }
  return "unknown signature result";
}

SignatureResult verifySignature(const uint8_t* payload, size_t length,
                                const uint8_t* signature,
                                const uint8_t* public_key) {
  uint8_t hash[32];
  if (mbedtls_sha256_ret(payload, length, hash, 0) != 0) {
    return SignatureResult::kCryptoError;
  }

  mbedtls_ecp_group group;
  mbedtls_ecp_point point;
  mbedtls_mpi r;
  mbedtls_mpi s;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&point);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);

  SignatureResult result = SignatureResult::kCryptoError;
  if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) != 0) {
    result = SignatureResult::kCryptoError;
  } else if (mbedtls_ecp_point_read_binary(&group, &point, public_key,
                                           kPublicKeyBytes) != 0 ||
             mbedtls_ecp_check_pubkey(&group, &point) != 0) {
    result = SignatureResult::kBadKey;
  } else if (mbedtls_mpi_read_binary(&r, signature, 32) != 0 ||
             mbedtls_mpi_read_binary(&s, signature + 32, 32) != 0) {
    result = SignatureResult::kCryptoError;
  } else {
    // Also rejects r or s outside [1, n-1].
    result = mbedtls_ecdsa_verify(&group, hash, sizeof(hash), &point, &r,
                                  &s) == 0
                 ? SignatureResult::kOk
                 : SignatureResult::kBadSignature;
  }

  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&point);
  mbedtls_ecp_group_free(&group);
  return result;
}

SignatureResult verifyEnvelope(const Envelope& envelope,
                               const uint8_t* payload, size_t length) {
  const TrustedKey* key = findTrustedKey(envelope.key_id);
  if (key == nullptr) return SignatureResult::kUnknownKey;
  return verifySignature(payload, length, envelope.signature, key->public_key);
}

}  // namespace fw
}  // namespace beamboy
