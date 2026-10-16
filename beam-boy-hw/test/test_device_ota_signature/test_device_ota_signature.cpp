// On-device test: the firmware's mbedTLS ECDSA P-256 verifier against the
// release tool's signed test vector.
//
// Host tests cover parsing and policy, but the signature check needs the
// target's mbedTLS, so this runs on a board:
//
//   pio test -e esp32-s3-devkitc-1-n16r8 -f test_device_ota_signature
//   pio test -e adafruit_feather_esp32s3_nopsram -f test_device_ota_signature
//
// It also logs verification time and heap use, which PLAN.md asks to measure
// before the signature format is adopted for real release keys.

#include <Arduino.h>
#include <unity.h>

#include <string.h>

#include "../ota_vectors/ota_test_vector.h"

// Device test builds do not compile src/, so pull in exactly the units under
// test.
#include "../../src/core/fw_manifest.cpp"
#include "../../src/core/ota_signature.cpp"

namespace beamboy {
namespace fw {

// Stands in for ota_keys.cpp: trusts only the throwaway test-vector key.
extern const TrustedKey kTrustedKeys[] = {
    {ota_test_vector::kKeyId, {OTA_TEST_VECTOR_PUBLIC_KEY}},
    {nullptr, {0}},
};

}  // namespace fw
}  // namespace beamboy

using namespace beamboy::fw;

namespace {

Envelope envelope;
uint8_t payload[kMaxPayloadBytes];
size_t payload_length = 0;

void loadVector() {
  const char* text = ota_test_vector::kEnvelope;
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    parseEnvelope(text, strlen(text), envelope));
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    decodePayload(envelope, payload, sizeof(payload),
                                  payload_length));
}

}  // namespace

void setUp() { loadVector(); }
void tearDown() {}

void test_vector_verifies() {
  const uint32_t heap_before = ESP.getFreeHeap();
  const uint32_t min_heap_before = ESP.getMinFreeHeap();
  const uint32_t start = micros();
  const SignatureResult result =
      verifySignature(payload, payload_length, envelope.signature,
                      ota_test_vector::kPublicKey);
  const uint32_t elapsed = micros() - start;
  TEST_ASSERT_EQUAL(SignatureResult::kOk, result);

  char line[128];
  snprintf(line, sizeof(line),
           "verify: %lu us, heap %lu -> %lu, min free heap %lu -> %lu",
           static_cast<unsigned long>(elapsed),
           static_cast<unsigned long>(heap_before),
           static_cast<unsigned long>(ESP.getFreeHeap()),
           static_cast<unsigned long>(min_heap_before),
           static_cast<unsigned long>(ESP.getMinFreeHeap()));
  TEST_MESSAGE(line);
}

void test_envelope_verifies_through_the_trusted_key_table() {
  TEST_ASSERT_EQUAL(SignatureResult::kOk,
                    verifyEnvelope(envelope, payload, payload_length));
}

void test_any_payload_change_is_rejected() {
  for (size_t i = 0; i < payload_length; i += 37) {
    payload[i] ^= 0x01;
    TEST_ASSERT_EQUAL(SignatureResult::kBadSignature,
                      verifyEnvelope(envelope, payload, payload_length));
    payload[i] ^= 0x01;
  }
  TEST_ASSERT_EQUAL(SignatureResult::kBadSignature,
                    verifyEnvelope(envelope, payload, payload_length - 1));
}

void test_any_signature_change_is_rejected() {
  for (size_t i = 0; i < kSignatureBytes; i += 7) {
    envelope.signature[i] ^= 0x80;
    TEST_ASSERT_EQUAL(SignatureResult::kBadSignature,
                      verifyEnvelope(envelope, payload, payload_length));
    envelope.signature[i] ^= 0x80;
  }
  memset(envelope.signature, 0, kSignatureBytes);  // r = s = 0.
  TEST_ASSERT_EQUAL(SignatureResult::kBadSignature,
                    verifyEnvelope(envelope, payload, payload_length));
}

void test_unknown_and_malformed_keys_are_rejected() {
  strcpy(envelope.key_id, "bb-release-1");
  TEST_ASSERT_EQUAL(SignatureResult::kUnknownKey,
                    verifyEnvelope(envelope, payload, payload_length));

  uint8_t off_curve[kPublicKeyBytes];
  memcpy(off_curve, ota_test_vector::kPublicKey, sizeof(off_curve));
  off_curve[64] ^= 0x01;
  TEST_ASSERT_EQUAL(SignatureResult::kBadKey,
                    verifySignature(payload, payload_length,
                                    envelope.signature, off_curve));

  uint8_t compressed[kPublicKeyBytes];
  memcpy(compressed, ota_test_vector::kPublicKey, sizeof(compressed));
  compressed[0] = 0x02;
  TEST_ASSERT_EQUAL(SignatureResult::kBadKey,
                    verifySignature(payload, payload_length,
                                    envelope.signature, compressed));
}

void test_signed_payload_parses() {
  Release release;
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    parsePayload(reinterpret_cast<const char*>(payload),
                                 payload_length, release));
  TEST_ASSERT_EQUAL_UINT32(7, release.sequence);
}

void setup() {
  // Give the USB CDC host time to attach before output starts.
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_vector_verifies);
  RUN_TEST(test_envelope_verifies_through_the_trusted_key_table);
  RUN_TEST(test_any_payload_change_is_rejected);
  RUN_TEST(test_any_signature_change_is_rejected);
  RUN_TEST(test_unknown_and_malformed_keys_are_rejected);
  RUN_TEST(test_signed_payload_parses);
  UNITY_END();
}

void loop() {}
