// Tests for the firmware manifest parser (core/fw_manifest.h).
//
// The parser is the first code to touch bytes from the network on the update
// path, and the signed payload it reads decides which image a console flashes.
// It must accept exactly the format the release tool writes -- proven here
// with the tool's own signed test vector -- and refuse everything else with a
// hard error: unknown or repeated fields, wrong types, out-of-range numbers,
// escapes, and anything trailing.

#include <unity.h>

#include <cstring>
#include <string>

#include "../ota_vectors/ota_test_vector.h"
#include "core/fw_manifest.h"

using namespace beamboy::fw;

namespace {

const char kSha[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string board(const char* id = "adafruit_feather_esp32s3_nopsram") {
  return std::string("{\"board\":\"") + id +
         "\",\"layout\":\"feather-s3-8mb-ota2048k-v1\",\"min_updater\":1,"
         "\"min_save\":3,\"min_api\":1,\"url\":\"https://github.com/x/y.bin\","
         "\"size\":1234,\"sha256\":\"" +
         kSha + "\"}";
}

std::string payload(const std::string& boards = "[" + board() + "]") {
  return "{\"schema\":\"beamboy-fw-manifest/1\",\"channel\":\"stable\","
         "\"version\":\"1.2.3\",\"sequence\":7,\"boards\":" +
         boards + "}";
}

// Replaces the first occurrence of `from`, failing the test if absent so a
// typo in a mutation cannot silently test the unmutated document.
std::string replace(std::string text, const std::string& from,
                    const std::string& to) {
  const size_t at = text.find(from);
  TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, from.c_str());
  return text.replace(at, from.size(), to);
}

ManifestError parse(const std::string& text) {
  Release release;
  return parsePayload(text.data(), text.size(), release);
}

// Base64 of 64 zero bytes.
std::string zeroSignature() { return std::string(86, 'A') + "=="; }

std::string envelope(const std::string& signature = zeroSignature()) {
  return "{\"schema\":\"beamboy-fw-envelope/1\",\"key_id\":\"bb-release-1\","
         "\"payload\":\"e30=\",\"signature\":\"" +
         signature + "\"}";
}

ManifestError parseEnv(const std::string& text) {
  Envelope out;
  return parseEnvelope(text.data(), text.size(), out);
}

std::string b64(const char* text) {
  uint8_t out[64];
  size_t length = 0;
  if (decodeBase64(text, strlen(text), out, sizeof(out), length) !=
      ManifestError::kNone) {
    return "<error>";
  }
  return std::string(reinterpret_cast<char*>(out), length);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_the_release_tool_vector_round_trips() {
  const char* text = ota_test_vector::kEnvelope;
  Envelope env;
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    parseEnvelope(text, strlen(text), env));
  TEST_ASSERT_EQUAL_STRING(ota_test_vector::kKeyId, env.key_id);

  static uint8_t decoded[kMaxPayloadBytes];
  size_t length = 0;
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    decodePayload(env, decoded, sizeof(decoded), length));

  Release release;
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    parsePayload(reinterpret_cast<const char*>(decoded),
                                 length, release));
  TEST_ASSERT_EQUAL_STRING("stable", release.channel);
  TEST_ASSERT_EQUAL_STRING("1.2.3", release.version);
  TEST_ASSERT_EQUAL_UINT32(7, release.sequence);
  TEST_ASSERT_EQUAL_UINT8(1, release.board_count);
  TEST_ASSERT_EQUAL_STRING("adafruit_feather_esp32s3_nopsram",
                           release.boards[0].board);
  TEST_ASSERT_EQUAL_STRING("feather-s3-8mb-ota2048k-v1",
                           release.boards[0].layout);
  TEST_ASSERT_EQUAL_UINT32(1234567, release.boards[0].size);
  TEST_ASSERT_EQUAL_UINT16(3, release.boards[0].min_save);
  TEST_ASSERT_EQUAL_STRING(
      "https://github.com/jakopako/beam-boy/releases/download/fw-v1.2.3/"
      "beamboy-fw-1.2.3-adafruit_feather_esp32s3_nopsram.bin",
      release.boards[0].url);
}

void test_parses_every_payload_field() {
  Release release;
  const std::string text = payload("[" + board() + "," +
                                   board("some-other-board") + "]");
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    parsePayload(text.data(), text.size(), release));
  const BoardImage& b = release.boards[0];
  TEST_ASSERT_EQUAL_UINT16(1, b.min_updater);
  TEST_ASSERT_EQUAL_UINT16(3, b.min_save);
  TEST_ASSERT_EQUAL_UINT16(1, b.min_api);
  TEST_ASSERT_EQUAL_UINT32(1234, b.size);
  TEST_ASSERT_EQUAL_STRING("https://github.com/x/y.bin", b.url);
  TEST_ASSERT_EQUAL_HEX8(0x01, b.sha256[0]);
  TEST_ASSERT_EQUAL_HEX8(0xef, b.sha256[31]);
  TEST_ASSERT_EQUAL_UINT8(2, release.board_count);
}

void test_tolerates_whitespace_between_tokens() {
  const std::string text =
      replace(replace(payload(), "{\"schema\"", " {\n\t\"schema\" "), "}]}",
              "} ]\r\n}\n");
  TEST_ASSERT_EQUAL(ManifestError::kNone, parse(text));
}

void test_rejects_unknown_duplicate_and_missing_payload_fields() {
  TEST_ASSERT_EQUAL(
      ManifestError::kUnknownField,
      parse(replace(payload(), "\"channel\"", "\"notes\":\"x\",\"channel\"")));
  TEST_ASSERT_EQUAL(ManifestError::kDuplicateField,
                    parse(replace(payload(), "\"sequence\":7",
                                  "\"sequence\":7,\"sequence\":8")));
  TEST_ASSERT_EQUAL(ManifestError::kMissingField,
                    parse(replace(payload(), "\"version\":\"1.2.3\",", "")));
  TEST_ASSERT_EQUAL(
      ManifestError::kUnknownField,
      parse(replace(payload(), "\"size\"", "\"extra\":1,\"size\"")));
  TEST_ASSERT_EQUAL(ManifestError::kDuplicateField,
                    parse(replace(payload(), "\"min_api\":1",
                                  "\"min_api\":1,\"min_api\":2")));
  TEST_ASSERT_EQUAL(ManifestError::kMissingField,
                    parse(replace(payload(), "\"min_save\":3,", "")));
}

void test_rejects_other_schemas_and_channels() {
  TEST_ASSERT_EQUAL(ManifestError::kUnsupportedSchema,
                    parse(replace(payload(), "manifest/1", "manifest/2")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), "\"stable\"", "\"Stable\"")));
  TEST_ASSERT_EQUAL(
      ManifestError::kBadString,
      parse(replace(payload(), "\"stable\"", "\"abcdefghijklmnopq\"")));
}

void test_rejects_malformed_numbers() {
  const std::string seq = "\"sequence\":7";
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), seq, "\"sequence\":0")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), seq, "\"sequence\":07")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), seq, "\"sequence\":-7")));
  TEST_ASSERT_EQUAL(ManifestError::kSyntax,
                    parse(replace(payload(), seq, "\"sequence\":7.5")));
  TEST_ASSERT_EQUAL(ManifestError::kSyntax,
                    parse(replace(payload(), seq, "\"sequence\":7e2")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), seq, "\"sequence\":\"7\"")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), seq, "\"sequence\":4294967296")));
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    parse(replace(payload(), seq, "\"sequence\":4294967295")));
  TEST_ASSERT_EQUAL(
      ManifestError::kBadValue,
      parse(replace(payload(), "\"min_save\":3", "\"min_save\":65536")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), "\"size\":1234", "\"size\":0")));
  TEST_ASSERT_EQUAL(
      ManifestError::kBadValue,
      parse(replace(payload(), "\"size\":1234", "\"size\":true")));
}

void test_rejects_bad_board_values() {
  TEST_ASSERT_EQUAL(
      ManifestError::kBadValue,
      parse(replace(payload(), "https://github.com", "http://github.com")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), "y.bin", "y z.bin")));
  TEST_ASSERT_EQUAL(
      ManifestError::kBadValue,
      parse(replace(payload(), "adafruit_feather", "Adafruit_feather")));
  TEST_ASSERT_EQUAL(
      ManifestError::kBadValue,
      parse(replace(payload(), "feather-s3-8mb", "feather/s3-8mb")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), "0123456789abcdef\"",
                                  "0123456789ABCDEF\"")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), "0123456789abcdef\"",
                                  "0123456789abcde\"")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parse(replace(payload(), "0123456789abcdef\"",
                                  "0123456789abcdef0\"")));
}

void test_rejects_bad_board_lists() {
  TEST_ASSERT_EQUAL(ManifestError::kBadValue, parse(payload("[]")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue, parse(payload(board())));
  TEST_ASSERT_EQUAL(ManifestError::kDuplicateBoard,
                    parse(payload("[" + board() + "," + board() + "]")));
  const std::string five = "[" + board("a") + "," + board("b") + "," +
                           board("c") + "," + board("d") + "," + board("e") +
                           "]";
  TEST_ASSERT_EQUAL(ManifestError::kTooManyBoards, parse(payload(five)));
  TEST_ASSERT_EQUAL(ManifestError::kSyntax,
                    parse(payload("[" + board() + ",]")));
}

void test_rejects_escapes_control_and_non_ascii_bytes() {
  TEST_ASSERT_EQUAL(ManifestError::kBadString,
                    parse(replace(payload(), "1.2.3", "1.2.\\u0033")));
  TEST_ASSERT_EQUAL(ManifestError::kBadString,
                    parse(replace(payload(), "y.bin", "y\\/.bin")));
  TEST_ASSERT_EQUAL(ManifestError::kBadString,
                    parse(replace(payload(), "y.bin", "y\t.bin")));
  TEST_ASSERT_EQUAL(ManifestError::kBadString,
                    parse(replace(payload(), "y.bin", "y\xc3\xa9.bin")));
  std::string with_nul = payload();
  with_nul[with_nul.find("1.2.3")] = '\0';
  TEST_ASSERT_EQUAL(ManifestError::kBadString, parse(with_nul));
}

void test_rejects_trailing_or_truncated_documents() {
  TEST_ASSERT_EQUAL(ManifestError::kSyntax, parse(payload() + "{}"));
  TEST_ASSERT_EQUAL(ManifestError::kSyntax, parse(payload() + ","));
  const std::string whole = payload();
  for (size_t cut = 0; cut < whole.size(); cut++) {
    TEST_ASSERT_TRUE(parse(whole.substr(0, cut)) != ManifestError::kNone);
  }
  TEST_ASSERT_EQUAL(ManifestError::kSyntax, parse("[]"));
  TEST_ASSERT_EQUAL(ManifestError::kSyntax, parse(""));
}

void test_rejects_oversize_documents_before_parsing() {
  const std::string huge(kMaxPayloadBytes + 1, ' ');
  TEST_ASSERT_EQUAL(ManifestError::kTooLarge, parse(huge));
  const std::string huge_env(kMaxEnvelopeBytes + 1, ' ');
  TEST_ASSERT_EQUAL(ManifestError::kTooLarge, parseEnv(huge_env));
}

void test_parses_envelope_fields() {
  const std::string text = envelope();
  Envelope env;
  TEST_ASSERT_EQUAL(ManifestError::kNone,
                    parseEnvelope(text.data(), text.size(), env));
  TEST_ASSERT_EQUAL_STRING("bb-release-1", env.key_id);
  TEST_ASSERT_EQUAL_UINT32(4, env.payload_b64_length);
  TEST_ASSERT_EQUAL_MEMORY("e30=", env.payload_b64, 4);
  for (size_t i = 0; i < kSignatureBytes; i++) {
    TEST_ASSERT_EQUAL_HEX8(0, env.signature[i]);
  }
}

void test_rejects_bad_envelopes() {
  TEST_ASSERT_EQUAL(ManifestError::kUnsupportedSchema,
                    parseEnv(replace(envelope(), "envelope/1", "envelope/2")));
  TEST_ASSERT_EQUAL(ManifestError::kUnknownField,
                    parseEnv(replace(envelope(), "\"key_id\"",
                                     "\"alg\":\"x\",\"key_id\"")));
  TEST_ASSERT_EQUAL(ManifestError::kDuplicateField,
                    parseEnv(replace(envelope(), "\"payload\"",
                                     "\"key_id\":\"a\",\"payload\"")));
  TEST_ASSERT_EQUAL(
      ManifestError::kMissingField,
      parseEnv(replace(envelope(), "\"payload\":\"e30=\",", "")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parseEnv(replace(envelope(), "bb-release-1", "BB_release")));
  TEST_ASSERT_EQUAL(ManifestError::kBadValue,
                    parseEnv(replace(envelope(), "\"e30=\"", "\"\"")));
  TEST_ASSERT_EQUAL(ManifestError::kSyntax, parseEnv(envelope() + "x"));
  // 63 and 66 bytes.
  TEST_ASSERT_EQUAL(ManifestError::kBadSignatureLength,
                    parseEnv(envelope(std::string(84, 'A'))));
  TEST_ASSERT_EQUAL(ManifestError::kBadSignatureLength,
                    parseEnv(envelope(std::string(88, 'A'))));
  TEST_ASSERT_EQUAL(ManifestError::kBadBase64,
                    parseEnv(envelope(std::string(86, 'A') + "=A")));
  TEST_ASSERT_EQUAL(ManifestError::kBadBase64,
                    parseEnv(envelope(std::string(86, 'A') + "B=")));
}

void test_base64_is_strict_rfc4648() {
  TEST_ASSERT_EQUAL_STRING("", b64("").c_str());
  TEST_ASSERT_EQUAL_STRING("f", b64("Zg==").c_str());
  TEST_ASSERT_EQUAL_STRING("fo", b64("Zm8=").c_str());
  TEST_ASSERT_EQUAL_STRING("foo", b64("Zm9v").c_str());
  TEST_ASSERT_EQUAL_STRING("foobar", b64("Zm9vYmFy").c_str());
  TEST_ASSERT_EQUAL_STRING("\xfb\xff", b64("+/8=").c_str());

  TEST_ASSERT_EQUAL_STRING("<error>", b64("Zg").c_str());  // No padding.
  TEST_ASSERT_EQUAL_STRING("<error>", b64("Zg=").c_str());
  TEST_ASSERT_EQUAL_STRING("<error>", b64("Zh==").c_str());  // Stray bits.
  TEST_ASSERT_EQUAL_STRING("<error>", b64("Zm9=").c_str());  // Stray bits.
  TEST_ASSERT_EQUAL_STRING("<error>", b64("Z===").c_str());
  TEST_ASSERT_EQUAL_STRING("<error>", b64("Zg==Zg==").c_str());
  TEST_ASSERT_EQUAL_STRING("<error>", b64("Zm9v\nYmFy").c_str());
  TEST_ASSERT_EQUAL_STRING("<error>", b64("Zm-_").c_str());  // URL-safe.

  uint8_t small[2];
  size_t length = 0;
  TEST_ASSERT_EQUAL(ManifestError::kTooLarge,
                    decodeBase64("Zm9v", 4, small, sizeof(small), length));
}

void test_versions_are_semver() {
  TEST_ASSERT_TRUE(isValidVersion("1.2.3"));
  TEST_ASSERT_TRUE(isValidVersion("0.0.0-dev"));
  TEST_ASSERT_TRUE(isValidVersion("10.20.30-rc.1"));
  TEST_ASSERT_FALSE(isValidVersion(""));
  TEST_ASSERT_FALSE(isValidVersion("1.2"));
  TEST_ASSERT_FALSE(isValidVersion("1.2.3.4"));
  TEST_ASSERT_FALSE(isValidVersion("01.2.3"));
  TEST_ASSERT_FALSE(isValidVersion("1.2.3-"));
  TEST_ASSERT_FALSE(isValidVersion("1.2.3+build"));
  TEST_ASSERT_FALSE(isValidVersion("v1.2.3"));
  TEST_ASSERT_TRUE(isValidVersion("1.2.3-aaaaaaaaaaaaaaaaa"));    // 23 chars.
  TEST_ASSERT_FALSE(isValidVersion("1.2.3-aaaaaaaaaaaaaaaaaa"));  // 24 chars.
  TEST_ASSERT_TRUE(isPrerelease("1.2.3-rc.1"));
  TEST_ASSERT_FALSE(isPrerelease("1.2.3"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_release_tool_vector_round_trips);
  RUN_TEST(test_parses_every_payload_field);
  RUN_TEST(test_tolerates_whitespace_between_tokens);
  RUN_TEST(test_rejects_unknown_duplicate_and_missing_payload_fields);
  RUN_TEST(test_rejects_other_schemas_and_channels);
  RUN_TEST(test_rejects_malformed_numbers);
  RUN_TEST(test_rejects_bad_board_values);
  RUN_TEST(test_rejects_bad_board_lists);
  RUN_TEST(test_rejects_escapes_control_and_non_ascii_bytes);
  RUN_TEST(test_rejects_trailing_or_truncated_documents);
  RUN_TEST(test_rejects_oversize_documents_before_parsing);
  RUN_TEST(test_parses_envelope_fields);
  RUN_TEST(test_rejects_bad_envelopes);
  RUN_TEST(test_base64_is_strict_rfc4648);
  RUN_TEST(test_versions_are_semver);
  return UNITY_END();
}
