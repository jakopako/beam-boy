#pragma once

// Beam Boy — installed cartridges on the filesystem.
//
// This is the step that makes an installed game *data* rather than code. The
// launcher's built-in table is fixed at build time; this scans `/games/` at
// boot and turns every valid folder found there into another launcher entry.
// Nothing above this layer knows the difference -- which is the whole point,
// because Phase 7's downloader then only has to write files into `/games/` for
// a game to appear.
//
// Layout, one folder per game:
//
//   /games/<id>/meta.json    { "id": "...", "title": "...", "color": "ff8000" }
//   /games/<id>/game.be      the Berry source, loaded on launch
//
// The folder name is authoritative for the id, not meta.json's `id` field --
// see CartridgeStore::scan(). Two cartridges cannot collide, because two
// folders cannot share a name; a cartridge colliding with a *built-in* game's
// id is rejected explicitly in loadMeta().
//
// Deliberate limits, since this runs on a device with a fixed RAM budget and
// will eventually be fed by files from the internet:
//
//   * A fixed-size cartridge table, so a filesystem with a thousand folders
//     cannot exhaust RAM. Extras are ignored with a log line.
//   * Metadata is copied into fixed-size buffers, so the launcher never holds
//     a pointer into a temporary read buffer.
//   * Script source is NOT held resident. Only metadata lives in RAM; the
//     script itself is read on launch and freed on exit, so the cost of an
//     installed-but-unplayed game is a few dozen bytes.

#include <Arduino.h>

#include "core/game_registry.h"
#include "vm/script_scene.h"

namespace beamboy {

// Upper bound on installed cartridges. Highscores are the real constraint:
// Storage's score table is shared between built-ins and cartridges, so it is
// sized to hold kGameCount + kMaxCartridges distinct ids. Without that, the
// first record set by the last-installed game would be silently dropped.
constexpr uint8_t kMaxCartridges = 12;

// Longest cartridge id, including the NUL. Must not exceed Storage's
// kGameIdLength, or a cartridge's highscore would be rejected at save time --
// scan() enforces this, so an over-long id is refused at install time where
// it's visible, rather than failing silently the first time someone sets a
// record.
constexpr uint8_t kMaxCartridgeIdLength = 12;
constexpr uint8_t kMaxCartridgeTitleLength = 24;

struct Cartridge {
  char id[kMaxCartridgeIdLength] = {0};
  char title[kMaxCartridgeTitleLength] = {0};
  // Path to the script, kept so launching doesn't have to rebuild it.
  char script_path[48] = {0};
  Color accent;
};

class CartridgeStore {
 public:
  // Scans /games/ and populates the table. Safe to call when the directory
  // doesn't exist (a console with no installed cartridges is the normal
  // first-boot state, not an error).
  //
  // Called once at boot rather than per launcher entry: enumerating a LittleFS
  // directory and parsing JSON takes long enough to drop frames, so it happens
  // before the frame loop starts.
  void scan();

  uint8_t count() const { return count_; }
  const Cartridge& at(uint8_t index) const { return cartridges_[index]; }

  // Reads a cartridge's script into a freshly allocated buffer the caller owns.
  // Returns nullptr if the file is missing or too large. The size cap matters:
  // this is loading a file that, after Phase 7, arrived over the network.
  static char* readScript(const char* path);

  static constexpr size_t kMaxScriptBytes = 32 * 1024;

 private:
  // Parses one /games/<id>/meta.json. Returns false if the file is missing,
  // malformed, or describes something we won't run.
  bool loadMeta(const char* dir_name, Cartridge& out);

  Cartridge cartridges_[kMaxCartridges];
  uint8_t count_ = 0;
};

// The registry the launcher actually reads: the built-in kGames table followed
// by everything CartridgeStore found, flattened into one list.
//
// A single list rather than the launcher merging two is what keeps every
// existing consumer -- launcher, engine score filing, beam.highscore() -- from
// having to learn that filesystem games exist.
class GameList {
 public:
  // Builds the merged list. Built-ins come first so their launcher positions
  // (and the stored "last played" index) don't shift when a game is installed
  // or deleted.
  void build(CartridgeStore& store);

  uint8_t count() const { return count_; }
  const GameEntry& at(uint8_t index) const { return entries_[index]; }

 private:
  static constexpr uint8_t kMaxEntries = 16 + kMaxCartridges;

  GameEntry entries_[kMaxEntries];
  // GameEntry stores id/title as const char*, so filesystem entries must point
  // at memory owned by this list rather than at the CartridgeStore passed to
  // build(). Boot uses a global store, but Phase 7 rescans from a temporary
  // store after installing a game; copying here keeps both paths safe.
  char cartridge_ids_[kMaxCartridges][kMaxCartridgeIdLength] = {};
  char cartridge_titles_[kMaxCartridges][kMaxCartridgeTitleLength] = {};
  // One host scene per installed cartridge. Each holds only a path until it is
  // entered, so this array is cheap despite being sized for the maximum.
  ScriptScene scenes_[kMaxCartridges];
  uint8_t count_ = 0;
};

// The single instance the launcher and engine read from.
GameList& gameList();

}  // namespace beamboy
