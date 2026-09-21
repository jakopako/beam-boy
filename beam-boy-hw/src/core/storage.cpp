#include "storage.h"

#include <LittleFS.h>
#include <string.h>

namespace beamboy {
namespace {

constexpr const char* kSavePath = "/beamboy.sav";

// Save format immediately before stable launcher ids were added. Reading it
// explicitly preserves brightness and highscores across the v2 -> v3 upgrade
// rather than treating a routine firmware update as a factory reset.
constexpr uint8_t kLegacyVersion = 2;
constexpr uint8_t kLegacyMaxScores = 24;
constexpr uint8_t kLegacyGameIdLength = 12;

struct LegacyScoreEntry {
  char game_id[kLegacyGameIdLength] = {0};
  uint32_t score = 0;
};

struct LegacySaveData {
  uint8_t version = kLegacyVersion;
  uint8_t brightness = 0;
  uint8_t last_game = 0;
  uint8_t score_count = 0;
  LegacyScoreEntry scores[kLegacyMaxScores];
};

uint32_t checksumBytes(const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < size; i++) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

}  // namespace

bool Storage::begin() {
#if defined(ESP32)
  // true = format automatically if the partition has never been used.
  mounted_ = LittleFS.begin(true);
#else
  mounted_ = LittleFS.begin();
  if (!mounted_) {
    LittleFS.format();
    mounted_ = LittleFS.begin();
  }
#endif

  if (!mounted_) return false;

  if (!load()) {
    // No save, or one we cannot trust. Start clean rather than guessing.
    data_ = SaveData();
    dirty_ = true;
  }
  return true;
}

// Deliberately simple: this guards against a truncated or half-written file,
// not against tampering. FNV-1a over every byte but the checksum itself.
uint32_t Storage::checksum(const SaveData& data) {
  return checksumBytes(&data, sizeof(data));
}

bool Storage::load() {
  if (!LittleFS.exists(kSavePath)) return false;

  File file = LittleFS.open(kSavePath, "r");
  if (!file) return false;

  const size_t file_size = file.size();

  if (file_size == sizeof(SaveData) + sizeof(uint32_t)) {
    SaveData loaded;
    uint32_t stored_checksum = 0;
    const size_t read =
        file.read(reinterpret_cast<uint8_t*>(&loaded), sizeof(loaded));
    const size_t read_sum = file.read(
        reinterpret_cast<uint8_t*>(&stored_checksum), sizeof(stored_checksum));
    file.close();

    if (read != sizeof(loaded) || read_sum != sizeof(stored_checksum)) {
      return false;
    }
    if (loaded.version != kVersion) return false;
    if (checksum(loaded) != stored_checksum) return false;
    if (loaded.score_count > kMaxScores) return false;

    data_ = loaded;
    data_.last_game_id[kGameIdLength - 1] = '\0';
    dirty_ = false;
    return true;
  }

  if (file_size == sizeof(LegacySaveData) + sizeof(uint32_t)) {
    static_assert(kLegacyGameIdLength <= kGameIdLength,
                  "legacy ids must fit the current save format");

    LegacySaveData legacy;
    uint32_t stored_checksum = 0;
    const size_t read =
        file.read(reinterpret_cast<uint8_t*>(&legacy), sizeof(legacy));
    const size_t read_sum = file.read(
        reinterpret_cast<uint8_t*>(&stored_checksum), sizeof(stored_checksum));
    file.close();

    if (read != sizeof(legacy) || read_sum != sizeof(stored_checksum)) {
      return false;
    }
    if (legacy.version != kLegacyVersion ||
        legacy.score_count > kLegacyMaxScores ||
        checksumBytes(&legacy, sizeof(legacy)) != stored_checksum) {
      return false;
    }

    data_ = SaveData();
    data_.brightness = legacy.brightness;
    data_.last_game = legacy.last_game;
    data_.score_count = legacy.score_count;
    for (uint8_t i = 0; i < legacy.score_count; i++) {
      memcpy(data_.scores[i].game_id, legacy.scores[i].game_id,
             kLegacyGameIdLength);
      data_.scores[i].game_id[kLegacyGameIdLength - 1] = '\0';
      data_.scores[i].score = legacy.scores[i].score;
    }
    // Rewritten as v3 on the next normal commit. last_game remains available
    // as the launcher's fallback until the next played game records its id.
    dirty_ = true;
    return true;
  }

  file.close();
  return false;
}

bool Storage::save() {
  if (!mounted_) return false;

  File file = LittleFS.open(kSavePath, "w");
  if (!file) return false;

  const uint32_t sum = checksum(data_);
  const size_t wrote_data =
      file.write(reinterpret_cast<const uint8_t*>(&data_), sizeof(SaveData));
  const size_t wrote_sum =
      file.write(reinterpret_cast<const uint8_t*>(&sum), sizeof(sum));
  file.close();

  // A short write leaves a file that load() will reject, losing every score.
  // Keep the data dirty so the next commit() retries rather than silently
  // dropping it -- clearing the flag here would make the loss permanent.
  if (wrote_data != sizeof(SaveData) || wrote_sum != sizeof(sum)) {
    return false;
  }

  data_.last_game_id[kGameIdLength - 1] = '\0';
  dirty_ = false;
  return true;
}

void Storage::commit() {
  if (!dirty_) return;
  save();
}

int Storage::findScore(const char* game_id) const {
  for (uint8_t i = 0; i < data_.score_count; i++) {
    // Compare only as many characters as are actually stored. Comparing all
    // kGameIdLength bytes would read past the stored NUL for a maximum-length
    // id and never match what submitScore() wrote, so that game's score could
    // never be found again -- and every save would append a duplicate entry.
    if (strncmp(data_.scores[i].game_id, game_id, kGameIdLength - 1) == 0) {
      return i;
    }
  }
  return -1;
}

uint32_t Storage::highscore(const char* game_id) const {
  const int index = findScore(game_id);
  return index >= 0 ? data_.scores[index].score : 0;
}

bool Storage::submitScore(const char* game_id, uint32_t score) {
  // Two ids sharing their first 11 characters would collide after truncation
  // and silently share a highscore. Refuse rather than corrupt: a game id that
  // does not fit is a registry bug, and it shows up the first time it is used.
  if (strlen(game_id) >= kGameIdLength) return false;

  const int index = findScore(game_id);

  if (index >= 0) {
    if (score <= data_.scores[index].score) return false;
    data_.scores[index].score = score;
    dirty_ = true;
    return true;
  }

  // A score of zero for a game we have never seen is not worth a slot.
  if (score == 0) return false;

  if (data_.score_count >= kMaxScores) return false;

  ScoreEntry& entry = data_.scores[data_.score_count];
  strncpy(entry.game_id, game_id, kGameIdLength - 1);
  entry.game_id[kGameIdLength - 1] = '\0';
  entry.score = score;
  data_.score_count++;
  dirty_ = true;
  return true;
}

void Storage::eraseScore(const char* game_id) {
  const int index = findScore(game_id);
  if (index < 0) return;

  // Compact rather than leave a hole: shift the last entry into the freed
  // slot and shrink the count, so score_count always matches exactly the
  // live entries findScore() and the kMaxScores cap rely on.
  const uint8_t last = data_.score_count - 1;
  if (static_cast<uint8_t>(index) != last) {
    data_.scores[index] = data_.scores[last];
  }
  data_.scores[last] = ScoreEntry();
  data_.score_count = last;
  dirty_ = true;
}

void Storage::setBrightness(uint8_t value) {
  if (data_.brightness == value) return;
  data_.brightness = value;
  dirty_ = true;
}

void Storage::setLastGame(uint8_t index) {
  if (data_.last_game == index) return;
  data_.last_game = index;
  dirty_ = true;
}

void Storage::setLastGameId(const char* id) {
  if (id == nullptr || strlen(id) >= kGameIdLength) return;
  if (strcmp(data_.last_game_id, id) == 0) return;
  strcpy(data_.last_game_id, id);
  dirty_ = true;
}

void Storage::reset() {
  data_ = SaveData();
  dirty_ = true;
  save();
}

}  // namespace beamboy
