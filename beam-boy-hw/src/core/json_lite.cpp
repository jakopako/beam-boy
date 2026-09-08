#include "core/json_lite.h"

namespace beamboy {
namespace json {
namespace {

void skipWhitespace(const char*& p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
}

// Reads a quoted string into out, which must hold kMaxValueLength + 1 bytes.
//
// Escapes are handled only to the extent the format needs: the two-character
// ones, plus \uXXXX which is *rejected* rather than decoded. Decoding \u
// correctly means UTF-8 encoding and surrogate pairs, and a game title has no
// need for it -- so it is refused outright rather than half-implemented, since
// a half-implemented escape decoder is exactly where parser bugs live.
bool readString(const char*& p, char* out, size_t out_size) {
  if (*p != '"') return false;
  p++;

  size_t len = 0;
  while (*p != '"') {
    // Catches both a genuinely unterminated string and one running off the end
    // of the buffer, since the input is always NUL terminated.
    if (*p == '\0') return false;

    char ch;
    if (*p == '\\') {
      p++;
      switch (*p) {
        case '"':
          ch = '"';
          break;
        case '\\':
          ch = '\\';
          break;
        case '/':
          ch = '/';
          break;
        case 'n':
          ch = '\n';
          break;
        case 't':
          ch = '\t';
          break;
        case 'r':
          ch = '\r';
          break;
        case 'b':
          ch = '\b';
          break;
        case 'f':
          ch = '\f';
          break;
        default:
          return false;  // includes \u and a trailing backslash
      }
      p++;
    } else {
      ch = *p++;
    }

    if (len + 1 >= out_size) return false;  // too long: reject, never truncate
    out[len++] = ch;
  }
  p++;  // closing quote

  out[len] = '\0';
  return true;
}

}  // namespace

bool parseFlatObject(const char* text, PairHandler handler, void* user) {
  if (text == nullptr || handler == nullptr) return false;

  const char* p = text;
  skipWhitespace(p);
  if (*p != '{') return false;
  p++;

  skipWhitespace(p);
  if (*p == '}') {
    // An empty object is well-formed JSON. It carries no fields, so the caller
    // will reject it for lacking an id -- that check belongs there, not here.
    p++;
    skipWhitespace(p);
    return *p == '\0';
  }

  char key[kMaxValueLength + 1];
  char value[kMaxValueLength + 1];

  for (;;) {
    skipWhitespace(p);
    if (!readString(p, key, sizeof(key))) return false;

    skipWhitespace(p);
    if (*p != ':') return false;
    p++;

    skipWhitespace(p);
    // Only string values. A nested object, array, number or bare literal is a
    // rejection rather than something to skip over: meta.json has no such
    // fields, so encountering one means the file is not what we think it is.
    if (!readString(p, value, sizeof(value))) return false;

    if (!handler(user, key, value)) return false;

    skipWhitespace(p);
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == '}') {
      p++;
      break;
    }
    return false;
  }

  // Trailing content means the document is not a single object; treat it as
  // malformed rather than ignoring whatever follows.
  skipWhitespace(p);
  return *p == '\0';
}

}  // namespace json
}  // namespace beamboy
