#pragma once

// Beam Boy — launcher.
//
// The problem: present a list on a display with no text, one pixel tall.
//
// The answer is that each game *is* a colour. The tube has ten fixed logical
// slots: up to nine games occupy slots 0-8 from the left, and Settings always
// occupies slot 9 at the far end. Any slots between the last game and Settings
// stay dark, so positions never shift and the launcher never scrolls.
//
//   Nav (stick)        change selection
//   A (release)        launch. On release rather than press, so that catching
//                      A a moment before B still reads as the A+B combo below
//                      instead of launching a game out from under it.
//   Nav press (hold)   show the selected game's highscore in binary,
//                      instantly, for as long as it's held
//   B (hold)           on an installed cartridge only: delete it, after
//                      a red countdown so it's never a surprise
//   A + B (hold)       show the battery gauge as a proportional bar, for as
//                      long as it's held -- launcher only, so a game never
//                      has to reserve this combo for itself

#include "core/cartridge_store.h"
#include "core/engine.h"
#include "scenes/launcher_gestures.h"

namespace beamboy {

class LauncherScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

 private:
  void renderList(Engine& engine);
  void renderBatteryGauge(Engine& engine);
  const GameEntry& selectedEntry() const;
  uint8_t selectedRegistryIndex() const;
  uint8_t selectedPhysicalSlot() const;
  void drawLauncherSlot(Display& display, uint8_t slot,
                        const Color& color, float intensity) const;

  // Dense navigation index: 0..gameCount()-1 are games; gameCount() is
  // Settings. Its physical slot is nevertheless always kSettingsSlot.
  uint8_t selected_ = 0;
  bool selection_initialized_ = false;
  bool settings_selected_ = false;

  // Eases between physical slots, so jumping over a dark gap toward Settings
  // still reads as movement in the correct direction.
  float highlight_ = 0.0f;

  // Set when a game is chosen; the launch waits for the flash animation.
  bool launching_ = false;
  float launch_timer_ = 0.0f;

  // True while the nav button has been held past kHighscoreHoldMs, so
  // render() knows to show the highscore instead of the normal list. The nav
  // button no longer launches, so this is a pure display flag -- nothing
  // reads it to gate a release-triggered launch.
  bool nav_hold_exceeded_ = false;

  // Set once a hold-B on an installed cartridge crosses kDeleteHoldMs, so the
  // deletion itself only fires once per hold rather than every frame past the
  // threshold, and so update() can hand off to render() which entry to wipe
  // the flash for after B is released.
  bool deleting_ = false;

  // True once A and B have been held together past kBatteryHoldMs, as decided
  // by gestures_.
  bool showing_battery_ = false;

  // Arbitrates the overlapping A / B / A+B gestures. Holds the latches that
  // make the order of pressing and releasing irrelevant; see
  // scenes/launcher_gestures.h.
  LauncherGestures gestures_;

  // This frame's arbitrated delete-hold duration, 0 when the gesture is
  // suppressed. Stored so render() draws its countdown from exactly the value
  // update() acts on, rather than re-deriving it from the raw button state and
  // risking the two disagreeing about whether a hold counts.
  uint32_t delete_hold_ms_ = 0;

  // Scratch store used only to rebuild gameList() after a delete --
  // CartridgeStore::scan() takes long enough that it must not run in the
  // frame loop for anything less rare than this.
  CartridgeStore rescan_store_;
};

}  // namespace beamboy
