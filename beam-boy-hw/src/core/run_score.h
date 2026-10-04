#pragma once

#include "storage.h"

namespace beamboy {

// Retain the best completed run in RAM before clearing the active run.
inline bool resetRunScore(Storage& storage, const char* game_id,
                          uint32_t& score) {
  if (game_id == nullptr || game_id[0] == '\0') {
    Serial.println("[score] cannot reset run without a game id");
    return false;
  }
  storage.submitScore(game_id, score);
  if (storage.highscore(game_id) < score) {
    Serial.println("[score] cannot preserve highscore; run was not reset");
    return false;
  }
  score = 0;
  return true;
}

}  // namespace beamboy
