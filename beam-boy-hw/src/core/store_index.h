#pragma once

// Beam Boy — downloadable cartridge index.
//
// Phase 7's store starts with one deliberately small JSON shape:
//
//   {
//     "games": [
//       {
//         "id": "reflexfs",
//         "title": "Reflex",
//         "color": "ff00aa",
//         "url": "https://example/games/reflexfs/game.be",
//         "sha256": "...64 lowercase/uppercase hex chars...",
//         "size": "1234"
//       }
//     ]
//   }
//
// All values are strings. That keeps the parser small and strict, matching the
// existing meta.json policy: downloaded data is rejected when it surprises us,
// never guessed at.

#include <stddef.h>
#include <stdint.h>

namespace beamboy {

class StoreIndex {
 public:
  static constexpr uint8_t kMaxEntries = 12;
  static constexpr uint8_t kMaxIdLength = 12;
  static constexpr uint8_t kMaxTitleLength = 24;
  static constexpr uint8_t kMaxColorLength = 8;  // "ff00aa" + NUL
  static constexpr uint8_t kMaxShaLength = 65;   // 64 hex + NUL
  static constexpr uint8_t kMaxUrlLength = 160;
  static constexpr size_t kMaxScriptBytes = 32 * 1024;

  struct Entry {
    char id[kMaxIdLength] = {0};
    char title[kMaxTitleLength] = {0};
    char color[kMaxColorLength] = {0};
    char url[kMaxUrlLength] = {0};
    char sha256[kMaxShaLength] = {0};
    size_t size = 0;
  };

  bool parse(const char* text);

  uint8_t count() const { return count_; }
  const Entry& at(uint8_t index) const { return entries_[index]; }

 private:
  Entry entries_[kMaxEntries];
  uint8_t count_ = 0;
};

}  // namespace beamboy
