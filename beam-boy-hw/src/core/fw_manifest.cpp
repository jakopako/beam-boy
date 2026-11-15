#include "core/fw_manifest.h"

#include <string.h>

namespace beamboy {
namespace fw {
namespace {

struct Cursor {
  const char* p;
  const char* end;
};

void skipWhitespace(Cursor& c) {
  while (c.p < c.end &&
         (*c.p == ' ' || *c.p == '\t' || *c.p == '\n' || *c.p == '\r')) {
    c.p++;
  }
}

bool consume(Cursor& c, char expected) {
  skipWhitespace(c);
  if (c.p >= c.end || *c.p != expected) return false;
  c.p++;
  return true;
}

// Peeks past whitespace without consuming the character.
bool next(Cursor& c, char expected) {
  skipWhitespace(c);
  return c.p < c.end && *c.p == expected;
}

// A string token as a span of the source. Only printable ASCII is allowed and
// no escapes: every field in the format is an identifier, a URL or base64.
ManifestError readRawString(Cursor& c, size_t max_length, const char*& start,
                            size_t& length) {
  skipWhitespace(c);
  if (c.p >= c.end) return ManifestError::kSyntax;
  if (*c.p != '"') return ManifestError::kBadValue;
  c.p++;
  start = c.p;
  while (c.p < c.end && *c.p != '"') {
    const unsigned char ch = static_cast<unsigned char>(*c.p);
    if (ch == '\\' || ch < 0x20 || ch > 0x7e) return ManifestError::kBadString;
    if (static_cast<size_t>(c.p - start) >= max_length) {
      return ManifestError::kBadString;
    }
    c.p++;
  }
  if (c.p >= c.end) return ManifestError::kSyntax;
  length = static_cast<size_t>(c.p - start);
  c.p++;
  return ManifestError::kNone;
}

ManifestError readString(Cursor& c, char* out, size_t max_length) {
  const char* start = nullptr;
  size_t length = 0;
  const ManifestError error = readRawString(c, max_length, start, length);
  if (error != ManifestError::kNone) return error;
  memcpy(out, start, length);
  out[length] = '\0';
  return ManifestError::kNone;
}

// A non-negative JSON integer without sign, fraction, exponent or leading
// zeros. Anything after the digits is left for the structural check.
ManifestError readUint(Cursor& c, uint32_t max, uint32_t& out) {
  skipWhitespace(c);
  if (c.p >= c.end) return ManifestError::kSyntax;
  if (*c.p < '0' || *c.p > '9') return ManifestError::kBadValue;
  if (*c.p == '0' && c.p + 1 < c.end && c.p[1] >= '0' && c.p[1] <= '9') {
    return ManifestError::kBadValue;
  }
  uint64_t value = 0;
  while (c.p < c.end && *c.p >= '0' && *c.p <= '9') {
    value = value * 10 + static_cast<uint64_t>(*c.p - '0');
    if (value > max) return ManifestError::kBadValue;
    c.p++;
  }
  out = static_cast<uint32_t>(value);
  return ManifestError::kNone;
}

// Looks a key up in a field table. Returns the index, or -1.
int fieldIndex(const char* key, size_t length, const char* const* names,
               int count) {
  for (int i = 0; i < count; i++) {
    if (strlen(names[i]) == length && memcmp(names[i], key, length) == 0) {
      return i;
    }
  }
  return -1;
}

// Reads `"key":` and resolves it against the table, rejecting repeats.
ManifestError readKey(Cursor& c, const char* const* names, int count,
                      uint32_t& seen, int& field) {
  const char* key = nullptr;
  size_t length = 0;
  ManifestError error = readRawString(c, 32, key, length);
  if (error == ManifestError::kBadValue) return ManifestError::kSyntax;
  if (error != ManifestError::kNone) return error;
  if (!consume(c, ':')) return ManifestError::kSyntax;
  field = fieldIndex(key, length, names, count);
  if (field < 0) return ManifestError::kUnknownField;
  const uint32_t bit = 1u << field;
  if (seen & bit) return ManifestError::kDuplicateField;
  seen |= bit;
  return ManifestError::kNone;
}

// After a value: ',' continues the object, '}' ends it.
ManifestError objectSeparator(Cursor& c, bool& done) {
  if (consume(c, ',')) {
    done = false;
    return ManifestError::kNone;
  }
  if (consume(c, '}')) {
    done = true;
    return ManifestError::kNone;
  }
  return ManifestError::kSyntax;
}

bool isLowerAlnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool isIdentifier(const char* text, bool allow_underscore) {
  if (text[0] == '\0') return false;
  for (const char* p = text; *p != '\0'; p++) {
    if (isLowerAlnum(*p) || *p == '-' || (allow_underscore && *p == '_')) {
      continue;
    }
    return false;
  }
  return true;
}

bool isLowerAlpha(const char* text) {
  if (text[0] == '\0') return false;
  for (const char* p = text; *p != '\0'; p++) {
    if (*p < 'a' || *p > 'z') return false;
  }
  return true;
}

bool isHttpsUrl(const char* url) {
  if (strncmp(url, "https://", 8) != 0 || url[8] == '\0') return false;
  for (const char* p = url; *p != '\0'; p++) {
    if (*p == ' ') return false;
  }
  return true;
}

int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

const char* const kEnvelopeFields[] = {"schema", "key_id", "payload",
                                       "signature"};
constexpr int kEnvelopeFieldCount = 4;

const char* const kPayloadFields[] = {"schema", "channel", "version",
                                      "sequence", "boards"};
constexpr int kPayloadFieldCount = 5;

const char* const kBoardFields[] = {"board",   "layout", "min_updater",
                                    "min_save", "min_api", "url",
                                    "size",    "sha256"};
constexpr int kBoardFieldCount = 8;

ManifestError readUint16(Cursor& c, uint16_t& out) {
  uint32_t value = 0;
  const ManifestError error = readUint(c, 0xffff, value);
  out = static_cast<uint16_t>(value);
  return error;
}

ManifestError parseBoard(Cursor& c, BoardImage& out) {
  if (!consume(c, '{')) return ManifestError::kSyntax;
  uint32_t seen = 0;
  bool done = false;
  while (!done) {
    int field = -1;
    ManifestError error =
        readKey(c, kBoardFields, kBoardFieldCount, seen, field);
    if (error != ManifestError::kNone) return error;

    switch (field) {
      case 0:
        error = readString(c, out.board, kMaxBoardIdLength);
        if (error == ManifestError::kNone && !isIdentifier(out.board, true)) {
          error = ManifestError::kBadValue;
        }
        break;
      case 1:
        error = readString(c, out.layout, kMaxLayoutIdLength);
        if (error == ManifestError::kNone && !isIdentifier(out.layout, true)) {
          error = ManifestError::kBadValue;
        }
        break;
      case 2:
        error = readUint16(c, out.min_updater);
        break;
      case 3:
        error = readUint16(c, out.min_save);
        break;
      case 4:
        error = readUint16(c, out.min_api);
        break;
      case 5:
        error = readString(c, out.url, kMaxUrlLength);
        if (error == ManifestError::kNone && !isHttpsUrl(out.url)) {
          error = ManifestError::kBadValue;
        }
        break;
      case 6:
        error = readUint(c, 0xffffffffu, out.size);
        if (error == ManifestError::kNone && out.size == 0) {
          error = ManifestError::kBadValue;
        }
        break;
      case 7: {
        char hex[kSha256Bytes * 2 + 1];
        error = readString(c, hex, sizeof(hex) - 1);
        if (error == ManifestError::kBadString) error = ManifestError::kBadValue;
        if (error == ManifestError::kNone &&
            !decodeHex(hex, out.sha256, kSha256Bytes)) {
          error = ManifestError::kBadValue;
        }
        break;
      }
    }
    if (error != ManifestError::kNone) return error;

    error = objectSeparator(c, done);
    if (error != ManifestError::kNone) return error;
  }
  if (seen != (1u << kBoardFieldCount) - 1) return ManifestError::kMissingField;
  return ManifestError::kNone;
}

ManifestError parseBoards(Cursor& c, Release& out) {
  if (!consume(c, '[')) return ManifestError::kBadValue;
  if (next(c, ']')) return ManifestError::kBadValue;  // At least one board.
  for (;;) {
    if (out.board_count >= kMaxBoards) return ManifestError::kTooManyBoards;
    BoardImage& board = out.boards[out.board_count];
    const ManifestError error = parseBoard(c, board);
    if (error != ManifestError::kNone) return error;
    for (uint8_t i = 0; i < out.board_count; i++) {
      if (strcmp(out.boards[i].board, board.board) == 0) {
        return ManifestError::kDuplicateBoard;
      }
    }
    out.board_count++;
    if (consume(c, ',')) continue;
    if (consume(c, ']')) return ManifestError::kNone;
    return ManifestError::kSyntax;
  }
}

ManifestError expectSchema(Cursor& c, const char* expected) {
  const char* start = nullptr;
  size_t length = 0;
  const ManifestError error = readRawString(c, 32, start, length);
  if (error == ManifestError::kBadString) {
    return ManifestError::kUnsupportedSchema;
  }
  if (error != ManifestError::kNone) return error;
  if (length != strlen(expected) || memcmp(start, expected, length) != 0) {
    return ManifestError::kUnsupportedSchema;
  }
  return ManifestError::kNone;
}

ManifestError expectEnd(Cursor& c) {
  skipWhitespace(c);
  return c.p == c.end ? ManifestError::kNone : ManifestError::kSyntax;
}

}  // namespace

const char* describe(ManifestError error) {
  switch (error) {
    case ManifestError::kNone:
      return "ok";
    case ManifestError::kTooLarge:
      return "manifest too large";
    case ManifestError::kSyntax:
      return "manifest syntax error";
    case ManifestError::kBadString:
      return "manifest string invalid or too long";
    case ManifestError::kUnknownField:
      return "manifest has an unknown field";
    case ManifestError::kDuplicateField:
      return "manifest has a duplicate field";
    case ManifestError::kMissingField:
      return "manifest is missing a field";
    case ManifestError::kUnsupportedSchema:
      return "manifest schema unsupported";
    case ManifestError::kBadValue:
      return "manifest value out of range";
    case ManifestError::kTooManyBoards:
      return "manifest lists too many boards";
    case ManifestError::kDuplicateBoard:
      return "manifest lists a board twice";
    case ManifestError::kBadBase64:
      return "manifest base64 invalid";
    case ManifestError::kBadSignatureLength:
      return "manifest signature has the wrong length";
  }
  return "unknown manifest error";
}

ManifestError decodeBase64(const char* text, size_t length, uint8_t* out,
                           size_t capacity, size_t& out_length) {
  out_length = 0;
  if (length % 4 != 0) return ManifestError::kBadBase64;
  if (length == 0) return ManifestError::kNone;

  size_t padding = 0;
  if (text[length - 1] == '=') padding++;
  if (text[length - 2] == '=') padding++;
  if (padding == 1 && text[length - 2] == '=') return ManifestError::kBadBase64;

  const size_t decoded = length / 4 * 3 - padding;
  if (decoded > capacity) return ManifestError::kTooLarge;

  size_t o = 0;
  for (size_t i = 0; i < length; i += 4) {
    const bool last = i + 4 == length;
    int v[4];
    for (int k = 0; k < 4; k++) {
      const bool pad_slot = last && k >= 4 - static_cast<int>(padding);
      if (pad_slot) {
        if (text[i + k] != '=') return ManifestError::kBadBase64;
        v[k] = 0;
        continue;
      }
      v[k] = base64Value(text[i + k]);
      if (v[k] < 0) return ManifestError::kBadBase64;
    }
    const uint32_t triple = (static_cast<uint32_t>(v[0]) << 18) |
                            (static_cast<uint32_t>(v[1]) << 12) |
                            (static_cast<uint32_t>(v[2]) << 6) |
                            static_cast<uint32_t>(v[3]);
    if (last && padding == 2 && (triple & 0xffff) != 0) {
      return ManifestError::kBadBase64;
    }
    if (last && padding == 1 && (triple & 0xff) != 0) {
      return ManifestError::kBadBase64;
    }
    out[o++] = static_cast<uint8_t>(triple >> 16);
    if (o < decoded) out[o++] = static_cast<uint8_t>(triple >> 8);
    if (o < decoded) out[o++] = static_cast<uint8_t>(triple);
  }
  out_length = decoded;
  return ManifestError::kNone;
}

ManifestError parseEnvelope(const char* text, size_t length, Envelope& out) {
  memset(&out, 0, sizeof(out));
  if (text == nullptr) return ManifestError::kSyntax;
  if (length > kMaxEnvelopeBytes) return ManifestError::kTooLarge;

  Cursor c = {text, text + length};
  if (!consume(c, '{')) return ManifestError::kSyntax;

  uint32_t seen = 0;
  bool done = false;
  while (!done) {
    int field = -1;
    ManifestError error =
        readKey(c, kEnvelopeFields, kEnvelopeFieldCount, seen, field);
    if (error != ManifestError::kNone) return error;

    switch (field) {
      case 0:
        error = expectSchema(c, BEAMBOY_FW_ENVELOPE_SCHEMA);
        break;
      case 1:
        error = readString(c, out.key_id, kMaxKeyIdLength);
        if (error == ManifestError::kNone && !isIdentifier(out.key_id, false)) {
          error = ManifestError::kBadValue;
        }
        break;
      case 2:
        error = readRawString(c, kMaxEnvelopeBytes, out.payload_b64,
                              out.payload_b64_length);
        if (error == ManifestError::kNone && out.payload_b64_length == 0) {
          error = ManifestError::kBadValue;
        }
        break;
      case 3: {
        const char* start = nullptr;
        size_t b64_length = 0;
        error = readRawString(c, 128, start, b64_length);
        if (error != ManifestError::kNone) break;
        uint8_t decoded[96];
        size_t decoded_length = 0;
        error = decodeBase64(start, b64_length, decoded, sizeof(decoded),
                             decoded_length);
        if (error == ManifestError::kTooLarge ||
            (error == ManifestError::kNone &&
             decoded_length != kSignatureBytes)) {
          error = ManifestError::kBadSignatureLength;
        }
        if (error == ManifestError::kNone) {
          memcpy(out.signature, decoded, kSignatureBytes);
        }
        break;
      }
    }
    if (error != ManifestError::kNone) return error;

    error = objectSeparator(c, done);
    if (error != ManifestError::kNone) return error;
  }
  if (seen != (1u << kEnvelopeFieldCount) - 1) {
    return ManifestError::kMissingField;
  }
  return expectEnd(c);
}

ManifestError decodePayload(const Envelope& envelope, uint8_t* out,
                            size_t capacity, size_t& out_length) {
  return decodeBase64(envelope.payload_b64, envelope.payload_b64_length, out,
                      capacity, out_length);
}

ManifestError parsePayload(const char* text, size_t length, Release& out) {
  memset(&out, 0, sizeof(out));
  if (text == nullptr) return ManifestError::kSyntax;
  if (length > kMaxPayloadBytes) return ManifestError::kTooLarge;

  Cursor c = {text, text + length};
  if (!consume(c, '{')) return ManifestError::kSyntax;

  uint32_t seen = 0;
  bool done = false;
  while (!done) {
    int field = -1;
    ManifestError error =
        readKey(c, kPayloadFields, kPayloadFieldCount, seen, field);
    if (error != ManifestError::kNone) return error;

    switch (field) {
      case 0:
        error = expectSchema(c, BEAMBOY_FW_PAYLOAD_SCHEMA);
        break;
      case 1:
        error = readString(c, out.channel, kMaxChannelLength);
        if (error == ManifestError::kNone && !isLowerAlpha(out.channel)) {
          error = ManifestError::kBadValue;
        }
        break;
      case 2:
        error = readString(c, out.version, kMaxVersionLength);
        if (error == ManifestError::kNone && !isValidVersion(out.version)) {
          error = ManifestError::kBadValue;
        }
        break;
      case 3:
        error = readUint(c, 0xffffffffu, out.sequence);
        if (error == ManifestError::kNone && out.sequence == 0) {
          error = ManifestError::kBadValue;
        }
        break;
      case 4:
        error = parseBoards(c, out);
        break;
    }
    if (error != ManifestError::kNone) return error;

    error = objectSeparator(c, done);
    if (error != ManifestError::kNone) return error;
  }
  if (seen != (1u << kPayloadFieldCount) - 1) {
    return ManifestError::kMissingField;
  }
  return expectEnd(c);
}

bool isValidVersion(const char* version) {
  const size_t length = strlen(version);
  if (length == 0 || length > kMaxVersionLength) return false;

  const char* p = version;
  for (int part = 0; part < 3; part++) {
    if (*p < '0' || *p > '9') return false;
    if (*p == '0' && p[1] >= '0' && p[1] <= '9') return false;
    while (*p >= '0' && *p <= '9') p++;
    if (part < 2) {
      if (*p != '.') return false;
      p++;
    }
  }
  if (*p == '\0') return true;
  if (*p != '-' || p[1] == '\0') return false;
  for (p++; *p != '\0'; p++) {
    const char c = *p;
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool isPrerelease(const char* version) {
  return strchr(version, '-') != nullptr;
}

bool decodeHex(const char* hex, uint8_t* out, size_t length) {
  if (strlen(hex) != length * 2) return false;
  for (size_t i = 0; i < length; i++) {
    const int hi = hexValue(hex[2 * i]);
    const int lo = hexValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

}  // namespace fw
}  // namespace beamboy
