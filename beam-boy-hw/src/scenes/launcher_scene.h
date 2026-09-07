#pragma once

// Beam Boy — launcher.
//
// The problem: present a list on a display with no text, one pixel tall.
//
// The answer is that each game *is* a colour. A game occupies a block of pixels
// in its own accent colour; the selected one is bright and breathing while the
// others sit dim. Scrolling moves the selection, not the list, so a game always
// lives at the same place on the tube and you learn its position physically —
// "Wormfight is the red one, second from the left" — the same way you learn
// where a menu item sits without reading it.
//
// If there are more games than the tube can show at a readable size, the list
// scrolls to keep the selection in view.
//
//   Nav (stick/wheel)  change selection
//   Nav press or A     launch
//   B (hold)           show the selected game's highscore in binary

#include "core/engine.h"

namespace beamboy {

class LauncherScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

 private:
  void renderList(Engine& engine);

  // How many pixels each game gets. Below three a game is hard to distinguish
  // from a stray lit pixel; the launcher shrinks blocks before it scrolls.
  uint8_t blockPixels(const Display& display) const;
  uint8_t visibleSlots(const Display& display) const;

  uint8_t selected_ = 0;
  uint8_t scroll_ = 0;

  // Eases toward `selected_` so the highlight slides rather than jumps, which
  // makes the direction of movement obvious on a display this small.
  float highlight_ = 0.0f;

  // Set when a game is chosen; the launch waits for the flash animation.
  bool launching_ = false;
  float launch_timer_ = 0.0f;
};

}  // namespace beamboy
