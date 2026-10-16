#pragma once

// Beam Boy — firmware update manifest: format and strict parser.
//
// The device fetches one small JSON *envelope* (stable.json):
//
//   {"schema":"beamboy-fw-envelope/1","key_id":"bb-release-1",
//    "payload":"<base64>","signature":"<base64>"}
//
// `signature` is ECDSA P-256 over SHA-256 of the exact decoded payload bytes,
// encoded as raw r||s (64 bytes). Signing the bytes, not a re-serialised JSON
// value, means the device and the release tool can never disagree about what
// was signed.
//
// The decoded payload is a second JSON document:
//
//   {"schema":"beamboy-fw-manifest/1","channel":"stable","version":"1.2.3",
//    "sequence":7,"boards":[{"board":"...","layout":"...","min_updater":1,
//    "min_save":3,"min_api":1,"url":"https://...","size":1234,
//    "sha256":"<64 lowercase hex>"}]}
//
// Both parsers accept exactly that shape. Every key is required, unknown and
// duplicate keys are rejected, values have strict types and bounded lengths,
// and strings may not contain escapes, control characters or non-ASCII bytes
// (nothing in the format needs them). A new field is a new schema, not a
// silently-ignored extra. As with json_lite.h, there is no recursion and every
// write is bounded.
//
// Parsing proves nothing about authenticity. Callers verify the signature
// (ota_signature.h) over the decoded payload *before* acting on parsePayload().

#include <stddef.h>
#include <stdint.h>

namespace beamboy {
namespace fw {

constexpr size_t kMaxEnvelopeBytes = 16 * 1024;
// Largest payload a maximal envelope's base64 could decode to.
constexpr size_t kMaxPayloadBytes = kMaxEnvelopeBytes / 4 * 3;
constexpr size_t kSignatureBytes = 64;
constexpr size_t kSha256Bytes = 32;

// String limits, excluding the NUL.
constexpr size_t kMaxKeyIdLength = 32;
constexpr size_t kMaxChannelLength = 16;
constexpr size_t kMaxVersionLength = 23;
constexpr size_t kMaxBoardIdLength = 40;
constexpr size_t kMaxLayoutIdLength = 40;
constexpr size_t kMaxUrlLength = 200;
constexpr uint8_t kMaxBoards = 4;

#define BEAMBOY_FW_ENVELOPE_SCHEMA "beamboy-fw-envelope/1"
#define BEAMBOY_FW_PAYLOAD_SCHEMA "beamboy-fw-manifest/1"

enum class ManifestError : uint8_t {
  kNone,
  kTooLarge,
  kSyntax,           // Not the expected JSON shape, or trailing content.
  kBadString,        // Escape, control character, non-ASCII or over-long.
  kUnknownField,
  kDuplicateField,
  kMissingField,
  kUnsupportedSchema,
  kBadValue,         // Well-formed but outside the field's allowed range.
  kTooManyBoards,
  kDuplicateBoard,
  kBadBase64,
  kBadSignatureLength,
};

const char* describe(ManifestError error);

struct Envelope {
  char key_id[kMaxKeyIdLength + 1];
  // Points into the parsed text; valid only while that buffer lives.
  const char* payload_b64;
  size_t payload_b64_length;
  uint8_t signature[kSignatureBytes];
};

struct BoardImage {
  char board[kMaxBoardIdLength + 1];
  char layout[kMaxLayoutIdLength + 1];
  uint16_t min_updater;
  uint16_t min_save;
  uint16_t min_api;
  char url[kMaxUrlLength + 1];
  uint32_t size;
  uint8_t sha256[kSha256Bytes];
};

struct Release {
  char channel[kMaxChannelLength + 1];
  char version[kMaxVersionLength + 1];
  uint32_t sequence;
  uint8_t board_count;
  BoardImage boards[kMaxBoards];
};

// Parses the envelope in place. `text` need not be NUL-terminated; an embedded
// NUL is a syntax error.
ManifestError parseEnvelope(const char* text, size_t length, Envelope& out);

// Strict RFC 4648 base64 (standard alphabet, mandatory padding, canonical
// trailing bits, no whitespace).
ManifestError decodeBase64(const char* text, size_t length, uint8_t* out,
                           size_t capacity, size_t& out_length);

ManifestError decodePayload(const Envelope& envelope, uint8_t* out,
                            size_t capacity, size_t& out_length);

// Parses decoded payload bytes. `text` need not be NUL-terminated.
ManifestError parsePayload(const char* text, size_t length, Release& out);

// True for "MAJOR.MINOR.PATCH" with an optional "-prerelease" suffix, numeric
// parts without leading zeros, at most kMaxVersionLength characters.
bool isValidVersion(const char* version);

// True if the version carries a "-prerelease" suffix.
bool isPrerelease(const char* version);

// Lowercase hex of exactly 2 * length characters.
bool decodeHex(const char* hex, uint8_t* out, size_t length);

}  // namespace fw
}  // namespace beamboy
