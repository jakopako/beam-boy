#include "core/cartridge_store.h"

#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>

#include "core/json_lite.h"

namespace beamboy {
namespace {

constexpr const char* kGamesDir = "/games";

// Largest meta.json accepted. A valid one is a couple of hundred bytes; the cap
// is here so a huge or truncated file is refused before it is read into RAM.
constexpr size_t kMaxMetaBytes = 512;

// Parses "ff8000" or "#ff8000" into a Color. Returns false on anything else,
// so a typo'd colour is a rejected cartridge rather than a black one that
// looks like a bug in the launcher.
bool parseHexColor(const char* text, Color& out) {
  if (text == nullptr) return false;
  if (*text == '#') text++;

  uint32_t value = 0;
  int digits = 0;
  for (; text[digits] != '\0'; digits++) {
    const char c = text[digits];
    uint8_t nibble;
    if (c >= '0' && c <= '9') {
      nibble = static_cast<uint8_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      nibble = static_cast<uint8_t>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      nibble = static_cast<uint8_t>(c - 'A' + 10);
    } else {
      return false;
    }
    if (digits >= 6) return false;
    value = (value << 4) | nibble;
  }
  if (digits != 6) return false;

  out = Color(static_cast<uint8_t>((value >> 16) & 0xFF),
              static_cast<uint8_t>((value >> 8) & 0xFF),
              static_cast<uint8_t>(value & 0xFF));
  return true;
}

// Collects the fields we care about as json_lite reports them. Unknown keys are
// ignored rather than rejected, so adding a field to the format later (an
// author name, a version) doesn't break consoles running older firmware.
struct MetaFields {
  char title[kMaxCartridgeTitleLength] = {0};
  Color accent = Color(255, 255, 255);
  bool has_color = false;
};

bool onMetaPair(void* user, const char* key, const char* value) {
  MetaFields* fields = static_cast<MetaFields*>(user);

  if (strcmp(key, "title") == 0) {
    // Rejected rather than truncated: a title cut off mid-word in the serial
    // listing would look like a firmware bug, not a bad cartridge.
    if (strlen(value) >= sizeof(fields->title)) return false;
    strcpy(fields->title, value);
  } else if (strcmp(key, "color") == 0) {
    if (!parseHexColor(value, fields->accent)) return false;
    fields->has_color = true;
  }
  return true;
}

// Reads a whole file into a NUL-terminated heap buffer, or nullptr.
char* readFile(const char* path, size_t max_bytes) {
  File file = LittleFS.open(path, "r");
  if (!file) return nullptr;

  const size_t size = file.size();
  if (size == 0 || size > max_bytes) {
    file.close();
    return nullptr;
  }

  char* buffer = static_cast<char*>(malloc(size + 1));
  if (buffer == nullptr) {
    file.close();
    return nullptr;
  }

  const size_t read = file.read(reinterpret_cast<uint8_t*>(buffer), size);
  file.close();

  if (read != size) {
    free(buffer);
    return nullptr;
  }
  buffer[size] = '\0';
  return buffer;
}

}  // namespace

char* CartridgeStore::readScript(const char* path) {
  return readFile(path, kMaxScriptBytes);
}

bool CartridgeStore::loadMeta(const char* dir_name, Cartridge& out) {
  // The folder name is the id, not meta.json's "id" field. The filesystem
  // already guarantees folder names are unique, so two *cartridges* cannot
  // collide -- whereas trusting the file would let two claim the same id and
  // silently share a highscore slot. A mismatched "id" field is simply ignored.
  if (strlen(dir_name) >= kMaxCartridgeIdLength) {
    Serial.print("[games] id too long, skipping: ");
    Serial.println(dir_name);
    return false;
  }

  // Uniqueness among folders is not enough: nothing stops a cartridge folder
  // being named "wormfight" and colliding with a *built-in* game's id, or
  // "store"/"network" and colliding with a fixed utility entry. Since
  // highscores are keyed by id string, that would let a downloaded cartridge
  // read and overwrite a built-in's record, or shadow a launcher-critical
  // utility scene. Refuse the id rather than let content from the games repo
  // shadow anything shipped in the firmware.
  for (uint8_t i = 0; i < games::kGameCount; i++) {
    if (strcmp(dir_name, games::kGames[i].id) == 0) {
      Serial.print("[games] id collides with a built-in game, skipping: ");
      Serial.println(dir_name);
      return false;
    }
  }
  for (uint8_t i = 0; i < games::kUtilityCount; i++) {
    if (strcmp(dir_name, games::kUtilities[i].id) == 0) {
      Serial.print("[games] id collides with a utility entry, skipping: ");
      Serial.println(dir_name);
      return false;
    }
  }

  char meta_path[64];
  snprintf(meta_path, sizeof(meta_path), "%s/%s/meta.json", kGamesDir,
           dir_name);

  char* text = readFile(meta_path, kMaxMetaBytes);
  if (text == nullptr) {
    Serial.print("[games] no readable meta.json in ");
    Serial.println(dir_name);
    return false;
  }

  MetaFields fields;
  const bool ok = json::parseFlatObject(text, onMetaPair, &fields);
  free(text);

  if (!ok) {
    Serial.print("[games] malformed meta.json in ");
    Serial.println(dir_name);
    return false;
  }

  char script_path[48];
  snprintf(script_path, sizeof(script_path), "%s/%s/game.be", kGamesDir,
           dir_name);
  if (!LittleFS.exists(script_path)) {
    Serial.print("[games] no game.be in ");
    Serial.println(dir_name);
    return false;
  }

  strcpy(out.id, dir_name);
  // A cartridge without a title falls back to its id, so it is still
  // launchable -- a missing title is cosmetic and shouldn't hide the game.
  if (fields.title[0] != '\0') {
    strcpy(out.title, fields.title);
  } else {
    strcpy(out.title, dir_name);
  }
  out.accent = fields.accent;
  strcpy(out.script_path, script_path);
  return true;
}

void CartridgeStore::scan() {
  count_ = 0;

  File dir = LittleFS.open(kGamesDir, "r");
  if (!dir || !dir.isDirectory()) {
    // Normal on a console that has never installed anything.
    if (dir) dir.close();
    return;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      if (count_ >= kMaxCartridges) {
        Serial.println("[games] cartridge limit reached, ignoring the rest");
        entry.close();
        break;
      }

      // name() may return either a bare name or a full path depending on core
      // version; take whatever follows the last '/' so both behave the same.
      const char* raw = entry.name();
      const char* slash = strrchr(raw, '/');
      const char* dir_name = slash ? slash + 1 : raw;

      Cartridge cartridge;
      if (loadMeta(dir_name, cartridge)) {
        cartridges_[count_++] = cartridge;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
}

void GameList::build(CartridgeStore& store) {
  count_ = 0;

  // Built-ins first, in their existing order, so installing a cartridge never
  // renumbers them -- Storage keeps the last-played *index*, and a shifting
  // list would make the console reopen on a different game after an install.
  for (uint8_t i = 0; i < games::kGameCount && count_ < kMaxEntries; i++) {
    entries_[count_++] = games::kGames[i];
  }

  for (uint8_t i = 0; i < store.count() && count_ < kMaxEntries; i++) {
    const Cartridge& cartridge = store.at(i);

    strncpy(cartridge_ids_[i], cartridge.id, sizeof(cartridge_ids_[i]) - 1);
    cartridge_ids_[i][sizeof(cartridge_ids_[i]) - 1] = '\0';
    strncpy(cartridge_titles_[i], cartridge.title,
            sizeof(cartridge_titles_[i]) - 1);
    cartridge_titles_[i][sizeof(cartridge_titles_[i]) - 1] = '\0';
    scenes_[i].setScriptPath(cartridge.script_path);

    GameEntry entry;
    entry.id = cartridge_ids_[i];
    entry.title = cartridge_titles_[i];
    entry.accent = cartridge.accent;
    entry.scene = &scenes_[i];
    // Filesystem cartridges are always games, never utility scenes: the engine
    // must own the pause/exit gesture for them, so a downloaded game can never
    // trap the player.
    entry.is_game = true;

    entries_[count_++] = entry;
  }

  // Store and Network always come last, in that order, after every built-in
  // and installed cartridge -- see game_registry.h. Appended here rather than
  // folded into kGames, precisely so the loop above can insert cartridges
  // between the two groups.
  for (uint8_t i = 0; i < games::kUtilityCount && count_ < kMaxEntries; i++) {
    entries_[count_++] = games::kUtilities[i];
  }
}

GameList& gameList() {
  static GameList list;
  return list;
}

}  // namespace beamboy
