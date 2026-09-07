#pragma once

// Beam Boy — persistent storage.
//
// Two things need to survive a power cycle: user settings, and per-game
// highscores. Both are small and fixed-size, so they are written as a single
// binary record rather than as JSON — parsing costs flash and RAM we would
// rather spend on games.
//
// The record is versioned and checksummed. A Beam Boy is expected to be
// reflashed often (that is the whole point of the cartridge model), and a struct
// layout will change between firmware versions. Rather than corrupting a save,
// a mismatched version or a bad checksum resets to defaults silently.
//
// Highscores are keyed by a game's string id, not by its index in the registry.
// Installing a new game must not shuffle everyone else's scores.

#include <Arduino.h>

namespace beamboy {

class Storage {
 public:
  // Mounts the filesystem, formatting it on first boot, and loads the save.
  // Returns false if the filesystem is unusable -- the console still runs, it
  // just cannot remember anything.
  bool begin();

  bool mounted() const { return mounted_; }

  // --- Highscores ----------------------------------------------------------

  uint32_t highscore(const char* game_id) const;

  // Records a score if it beats the stored one. Returns true if it was a new
  // record, so the game can celebrate. Writes are deferred, not immediate --
  // see commit().
  bool submitScore(const char* game_id, uint32_t score);

  // --- Settings ------------------------------------------------------------

  uint8_t brightness() const { return data_.brightness; }
  void setBrightness(uint8_t value);

  // Index of the game shown when the launcher opens, so the console returns to
  // whatever was played last.
  uint8_t lastGame() const { return data_.last_game; }
  void setLastGame(uint8_t index);

  // --- Persistence ---------------------------------------------------------

  // Writes to flash only if something changed. Called at moments when a pause
  // is invisible -- returning to the launcher, not mid-game -- because a flash
  // write takes long enough to drop a frame.
  void commit();

  bool dirty() const { return dirty_; }

  // Wipes everything back to defaults.
  void reset();

 private:
  static constexpr uint8_t kVersion = 1;
  static constexpr uint8_t kMaxScores = 12;
  // Storage key size, including the terminating NUL. An id must therefore be at
  // most kGameIdLength - 1 characters; submitScore() rejects longer ones rather
  // than truncating, since two ids sharing a prefix would silently collide.
  static constexpr uint8_t kGameIdLength = 12;

  struct ScoreEntry {
    char game_id[kGameIdLength] = {0};
    uint32_t score = 0;
  };

  struct SaveData {
    uint8_t version = kVersion;
    uint8_t brightness = 0;  // 0 means "use the board default"
    uint8_t last_game = 0;
    uint8_t score_count = 0;
    ScoreEntry scores[kMaxScores];
  };

  static uint32_t checksum(const SaveData& data);
  bool load();
  bool save();

  int findScore(const char* game_id) const;

  SaveData data_;
  bool mounted_ = false;
  bool dirty_ = false;
};

}  // namespace beamboy
