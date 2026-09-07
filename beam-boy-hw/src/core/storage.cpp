#include "storage.h"

#include <LittleFS.h>
#include <string.h>

namespace beamboy {
namespace {

constexpr const char* kSavePath = "/beamboy.sav";

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
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(SaveData); i++) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

bool Storage::load() {
  if (!LittleFS.exists(kSavePath)) return false;

  File file = LittleFS.open(kSavePath, "r");
  if (!file) return false;

  SaveData loaded;
  uint32_t stored_checksum = 0;

  const size_t want = sizeof(SaveData);
  const bool sized_right =
      file.size() == want + sizeof(stored_checksum);

  if (!sized_right) {
    file.close();
    return false;
  }

  const size_t read = file.read(reinterpret_cast<uint8_t*>(&loaded), want);
  const size_t read_sum = file.read(
      reinterpret_cast<uint8_t*>(&stored_checksum), sizeof(stored_checksum));
  file.close();

  if (read != want || read_sum != sizeof(stored_checksum)) return false;
  if (loaded.version != kVersion) return false;
  if (checksum(loaded) != stored_checksum) return false;
  if (loaded.score_count > kMaxScores) return false;

  data_ = loaded;
  dirty_ = false;
  return true;
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

void Storage::reset() {
  data_ = SaveData();
  dirty_ = true;
  save();
}

}  // namespace beamboy
