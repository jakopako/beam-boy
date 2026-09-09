#include "core/store_index.h"

#include <stdlib.h>
#include <string.h>

namespace beamboy {
namespace {

// Must stay in sync with StoreIndex::kMaxUrlLength. Kept as a plain constant
// here rather than ODR-using the static constexpr on older embedded C++ modes.
constexpr size_t kMaxValueLength = 160;

void skipWhitespace(const char*& p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
}

bool readChar(const char*& p, char expected) {
  skipWhitespace(p);
  if (*p != expected) return false;
  p++;
  return true;
}

bool readString(const char*& p, char* out, size_t out_size) {
  skipWhitespace(p);
  if (*p != '"') return false;
  p++;

  size_t len = 0;
  while (*p != '"') {
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
          return false;
      }
      p++;
    } else {
      ch = *p++;
    }

    if (len + 1 >= out_size) return false;
    out[len++] = ch;
  }
  p++;

  out[len] = '\0';
  return true;
}

bool copyField(char* out, size_t out_size, const char* value) {
  if (strlen(value) >= out_size) return false;
  strcpy(out, value);
  return true;
}

bool validText(const char* text) {
  for (const char* p = text; *p != '\0'; p++) {
    if (static_cast<unsigned char>(*p) < 32) return false;
  }
  return true;
}

bool isHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

bool validId(const char* id) {
  if (id[0] == '\0') return false;
  for (const char* p = id; *p != '\0'; p++) {
    const char c = *p;
    const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    const bool digit = c >= '0' && c <= '9';
    if (!alpha && !digit && c != '-' && c != '_') return false;
  }
  return true;
}

bool validColor(const char* color) {
  if (strlen(color) != 6) return false;
  for (uint8_t i = 0; i < 6; i++) {
    if (!isHex(color[i])) return false;
  }
  return true;
}

bool validSha256(const char* sha) {
  if (strlen(sha) != 64) return false;
  for (uint8_t i = 0; i < 64; i++) {
    if (!isHex(sha[i])) return false;
  }
  return true;
}

bool parseSize(const char* text, size_t& out) {
  if (text[0] == '\0') return false;
  for (const char* p = text; *p != '\0'; p++) {
    if (*p < '0' || *p > '9') return false;
  }

  char* end = nullptr;
  const unsigned long value = strtoul(text, &end, 10);
  if (end == nullptr || *end != '\0') return false;
  if (value == 0 || value > StoreIndex::kMaxScriptBytes) return false;
  out = static_cast<size_t>(value);
  return true;
}

bool validUrl(const char* url) {
  if (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0) {
    return false;
  }
  for (const char* p = url; *p != '\0'; p++) {
    if (static_cast<unsigned char>(*p) <= 32) return false;
  }
  return true;
}

bool readEntry(const char*& p, StoreIndex::Entry& out) {
  if (!readChar(p, '{')) return false;

  bool has_id = false;
  bool has_title = false;
  bool has_color = false;
  bool has_url = false;
  bool has_sha = false;
  bool has_size = false;

  char key[kMaxValueLength] = {0};
  char value[kMaxValueLength] = {0};

  skipWhitespace(p);
  if (*p == '}') return false;

  for (;;) {
    if (!readString(p, key, sizeof(key))) return false;
    if (!readChar(p, ':')) return false;
    if (!readString(p, value, sizeof(value))) return false;

    if (strcmp(key, "id") == 0) {
      if (has_id) return false;
      if (!copyField(out.id, sizeof(out.id), value)) return false;
      has_id = true;
    } else if (strcmp(key, "title") == 0) {
      if (has_title) return false;
      if (!copyField(out.title, sizeof(out.title), value)) return false;
      has_title = true;
    } else if (strcmp(key, "color") == 0) {
      if (has_color) return false;
      if (!copyField(out.color, sizeof(out.color), value)) return false;
      has_color = true;
    } else if (strcmp(key, "url") == 0) {
      if (has_url) return false;
      if (!copyField(out.url, sizeof(out.url), value)) return false;
      has_url = true;
    } else if (strcmp(key, "sha256") == 0) {
      if (has_sha) return false;
      if (!copyField(out.sha256, sizeof(out.sha256), value)) return false;
      has_sha = true;
    } else if (strcmp(key, "size") == 0) {
      if (has_size) return false;
      if (!parseSize(value, out.size)) return false;
      has_size = true;
    }

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

  return has_id && has_title && has_color && has_url && has_sha && has_size &&
         validId(out.id) && validText(out.title) && validColor(out.color) &&
         validUrl(out.url) && validSha256(out.sha256);
}

}  // namespace

bool StoreIndex::parse(const char* text) {
  count_ = 0;
  if (text == nullptr) return false;

  const char* p = text;
  if (!readChar(p, '{')) return false;

  char key[kMaxValueLength] = {0};
  if (!readString(p, key, sizeof(key))) return false;
  if (strcmp(key, "games") != 0) return false;
  if (!readChar(p, ':')) return false;
  if (!readChar(p, '[')) return false;

  skipWhitespace(p);
  if (*p != ']') {
    for (;;) {
      if (count_ >= kMaxEntries) return false;
      Entry entry;
      if (!readEntry(p, entry)) return false;
      for (uint8_t i = 0; i < count_; i++) {
        if (strcmp(entries_[i].id, entry.id) == 0) return false;
      }
      entries_[count_++] = entry;

      skipWhitespace(p);
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == ']') break;
      return false;
    }
  }
  p++;

  if (!readChar(p, '}')) return false;
  skipWhitespace(p);
  return *p == '\0';
}

}  // namespace beamboy
