#include "scenes/launcher_scene.h"

#include <math.h>

#include "core/cartridge_store.h"
#include "core/game_registry.h"
#include "core/launcher_policy.h"
#include "scenes/launcher_delete_feedback.h"

namespace beamboy {
namespace {

constexpr float kHighlightEase = 12.0f;
constexpr float kLaunchFlashTime = 0.35f;

// How long the nav button must be held before it shows the highscore instead
// of launching on release. Comfortably longer than an intentional tap (a
// physical click rarely takes this long even when deliberate), short enough
// that peeking at a score doesn't feel like a separate mode.
constexpr uint32_t kHighscoreHoldMs = 350;

// kDeleteHoldMs and kBatteryHoldMs live in scenes/launcher_gestures.h,
// alongside the arbitration that enforces them.

}  // namespace

void LauncherScene::enter(Engine& engine) {
  if (!selection_initialized_) {
    // Prefer the stable id so installing/deleting another cartridge cannot
    // move the selection onto an unrelated game. Saves from before the id
    // existed fall back to the legacy index and are upgraded next time a game
    // exits.
    const int8_t saved_by_id =
        gameList().indexOfId(engine.storage().lastGameId());
    if (saved_by_id >= 0) {
      selected_ = static_cast<uint8_t>(saved_by_id);
    } else {
      selected_ = engine.storage().lastGame();
      if (selected_ >= gameList().gameCount()) selected_ = 0;
    }
    settings_selected_ = gameList().gameCount() == 0;
    selection_initialized_ = true;
  } else if (settings_selected_) {
    // A Store install can increase gameCount() while Settings is open. Its
    // dense selection index therefore moves, even though its physical slot
    // does not.
    selected_ = reconcileLauncherSelection(selected_, true,
                                           gameList().gameCount());
  } else {
    selected_ = reconcileLauncherSelection(selected_, false,
                                           gameList().gameCount());
  }

  highlight_ = static_cast<float>(selectedPhysicalSlot());
  launching_ = false;
  launch_timer_ = 0.0f;
  nav_hold_exceeded_ = false;
  deleting_ = false;
  delete_confirmation_ = false;
  showing_battery_ = false;
  delete_hold_ms_ = 0;
  gestures_.reset();

  engine.setCurrentGame(-1);
  engine.display().clear();
}

uint8_t LauncherScene::selectedRegistryIndex() const {
  return selected_ < gameList().gameCount() ? selected_
                                            : gameList().settingsIndex();
}

const GameEntry& LauncherScene::selectedEntry() const {
  return gameList().at(selectedRegistryIndex());
}

uint8_t LauncherScene::selectedPhysicalSlot() const {
  return launcherSlotForSelection(selected_, gameList().gameCount());
}

void LauncherScene::update(Engine& engine, float dt) {
  Input& input = engine.input();

  if (delete_confirmation_) {
    if (millis() - deleted_at_ms_ < kDeleteConfirmationMs) return;
    delete_confirmation_ = false;
  }

  if (launching_) {
    launch_timer_ -= dt;
    if (launch_timer_ <= 0.0f) {
      const uint8_t registry_index = selectedRegistryIndex();
      Scene* scene = gameList().at(registry_index).scene;
      if (scene != nullptr) {
        engine.setUtilityReturn(nullptr);
        engine.setCurrentGame(static_cast<int8_t>(registry_index));
        engine.setScene(scene);
      } else {
        launching_ = false;
      }
    }
    return;
  }

  // Navigation comes through navDelta(), which turns horizontal stick
  // deflection into discrete steps. The navigation list is dense even though
  // its physical layout has a dark gap: the step after the last game lands
  // directly on Settings at slot 9.
  const int8_t step = input.navDelta();
  const uint8_t previous_selection = selected_;
  if (step != 0) {
    const uint8_t settings_selection = gameList().gameCount();
    // Clamp rather than wrap: on a physical line, running off the end and
    // reappearing at the other is disorienting.
    selected_ =
        moveLauncherSelection(selected_, settings_selection, step);
    settings_selected_ = selected_ == settings_selection;
  }

  highlight_ +=
      (static_cast<float>(selectedPhysicalSlot()) - highlight_) *
      kHighlightEase * dt;

  // A, B and A+B overlap on just two buttons, so which gesture this frame's
  // input belongs to is arbitrated in one place -- see
  // scenes/launcher_gestures.h for the ordering traps that logic exists to
  // close, and test_launcher_gestures for the cases that pin it down.
  ButtonSnapshot buttons;
  buttons.selection_changed = selected_ != previous_selection;
  buttons.a_down = input.held(Button::kA);
  buttons.b_down = input.held(Button::kB);
  buttons.a_released = input.released(Button::kA);
  buttons.a_hold_ms = input.holdDuration(Button::kA);
  buttons.b_hold_ms = input.holdDuration(Button::kB);

  const GestureResult gesture = gestures_.update(buttons);
  showing_battery_ = gesture.show_battery;
  // Stashed for render(), so the delete countdown it draws and the deletion
  // fired below are driven by the same arbitrated number rather than each
  // re-deriving it and risking disagreement.
  delete_hold_ms_ = gesture.delete_hold_ms;

  if (gesture.launch) {
    launching_ = true;
    launch_timer_ = kLaunchFlashTime;
  }

  // The nav button/stick no longer launches -- A is the only way to launch,
  // so there is exactly one gesture for it. Holding the nav button still
  // shows the selected game's highscore; nav_hold_exceeded_ just latches once
  // the hold clears the threshold, so render() knows to show it.
  nav_hold_exceeded_ = input.heldFor(Input::kNavButton, kHighscoreHoldMs);

  // Deleting only applies to installed cartridges -- a built-in or a utility
  // scene (Store, Network) must never disappear from the launcher this way.
  if (!deleting_ && selected_ < gameList().gameCount() &&
      selectedEntry().is_installed &&
      delete_hold_ms_ >= kDeleteHoldMs) {
    // Present the completed warning before the filesystem work removes it.
    deleted_slot_ = selectedPhysicalSlot();
    engine.display().clear();
    renderList(engine);
    drawLauncherSlot(engine.display(), deleted_slot_, colors::kRed, 1.0f);
    engine.display().present();
    deleting_ = true;
    const char* deleted_id = selectedEntry().id;
    if (!CartridgeStore::remove(deleted_id)) {
      Serial.println("[launcher] uninstall failed; highscore retained");
      gestures_.reset();
      delete_hold_ms_ = 0;
      return;
    }
    delete_confirmation_ = true;
    // The cartridge is gone; its highscore must go with it, or a later game
    // that happens to reuse the same id would inherit a score it never
    // earned. Flush immediately -- deletion is deliberate and rare enough
    // that a flash write here is not worth deferring to the next commit().
    engine.storage().eraseScore(deleted_id);
    rescan_store_.scan();
    gameList().build(rescan_store_);
    // The list just shrank. Stay on the game that moved into this slot, or the
    // preceding game when the deleted one was last. Settings is still one
    // navigation step beyond the remaining games.
    selected_ = reconcileLauncherSelection(selected_, false,
                                           gameList().gameCount());
    settings_selected_ = gameList().gameCount() == 0;
    if (gameList().gameCount() > 0) {
      engine.storage().setLastGame(selected_);
      engine.storage().setLastGameId(selectedEntry().id);
    } else {
      engine.storage().setLastGame(0);
      engine.storage().setLastGameId("");
    }
    engine.storage().commit();
    highlight_ = static_cast<float>(selectedPhysicalSlot());
    deleted_at_ms_ = millis();
  } else if (!input.held(Button::kB)) {
    deleting_ = false;
  }
}

void LauncherScene::renderList(Engine& engine) {
  Display& display = engine.display();

  for (uint8_t index = 0; index < gameList().gameCount(); index++) {
    const GameEntry& game = gameList().at(index);

    // Distance from the (eased) highlight drives brightness, so the selection
    // reads as a glow moving along the line rather than a discrete jump.
    const float distance = fabsf(highlight_ - static_cast<float>(index));
    float intensity;
    if (distance < 1.0f) {
      intensity = 0.22f + 0.78f * (1.0f - distance);
    } else {
      intensity = 0.22f;
    }

    // Breathing is keyed off selected_, not off proximity to the eased
    // highlight_. highlight_ decays exponentially toward selected_ and, in
    // floating point, never quite reaches it -- so the previously-selected
    // neighbour sits at a distance just under 1.0f indefinitely, which used
    // to be enough to fall into the branch above and pick up the same
    // breathing level, leaving it pulsing right along with the real
    // selection. Restricting this to an exact index match keeps the glow
    // (still driven by distance, for the moving-highlight animation) but
    // limits breathing to the one slot that is actually selected.
    Color color = game.accent;
    if (index == selected_ && game.is_installed && !deleting_ &&
        delete_hold_ms_ > 0) {
      const DeleteFeedback feedback =
          launcherDeleteFeedback(game.accent, delete_hold_ms_);
      color = feedback.color;
      intensity = feedback.intensity;
    } else if (index == selected_) {
      const float level = 0.75f + 0.25f * pulse(millis() / 1000.0f, 4.0f);
      intensity *= level;
    }

    drawLauncherSlot(display, index, color, intensity);
  }

  const uint8_t settings_selection = gameList().gameCount();
  const float distance = fabsf(highlight_ - static_cast<float>(kSettingsSlot));
  float intensity =
      distance < 1.0f ? 0.22f + 0.78f * (1.0f - distance) : 0.22f;
  if (selected_ == settings_selection) {
    intensity *=
        0.75f + 0.25f * pulse(millis() / 1000.0f, 4.0f);
  }
  drawLauncherSlot(display, kSettingsSlot, gameList().settings().accent,
                   intensity);
}

void LauncherScene::drawLauncherSlot(Display& display, uint8_t slot,
                                     const Color& color,
                                     float intensity) const {
  uint16_t start = launcherSlotStart(slot, display.pixelCount());
  uint16_t end = launcherSlotEnd(slot, display.pixelCount());
  if (end <= start) return;

  // On a multi-pixel slot, leave one dark pixel between adjacent games. For
  // Settings the gap goes before the block so its final lit pixel remains at
  // the physical end of the tube. A ten-pixel prototype has one pixel per slot
  // and therefore no room for an additional separator.
  if (end - start > 1) {
    if (slot == kSettingsSlot) {
      start++;
    } else {
      end--;
    }
  }
  for (uint16_t pixel = start; pixel < end; pixel++) {
    display.rawPixel(pixel, color.scaled(intensity));
  }
}

void LauncherScene::renderBatteryGauge(Engine& engine) {
  Display& display = engine.display();
  Power& power = engine.power();

  if (!power.available()) {
    // No fuel gauge on this board (the DevKitC has none) -- a dim, steady
    // white pixel says "nothing to show" rather than looking like a bug or,
    // worse, an empty (0%) battery.
    display.point(0.5f, Color(40, 40, 40), 1.0f);
    return;
  }

  const float fraction = power.percent() / 100.0f;
  const Color color = fraction > 0.5f
                           ? colors::kGreen
                           : (fraction > 0.2f ? colors::kAmber : colors::kRed);

  // A proportional bar rather than another binary readout: this is meant as
  // an instant, low-fidelity glance, and a bar reads faster than counting
  // bits for a number nobody needs to be precise about.
  float level = 1.0f;
  if (power.charging()) {
    // Breathing signals "still filling up" -- there is no cable icon to draw
    // on a one-dimensional display, so the bar itself pulses instead.
    level = 0.55f + 0.45f * pulse(millis() / 1000.0f, 2.0f);
  }
  display.span(0.0f, fraction, color, level);
}

void LauncherScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  // Checked before anything else: the gauge doesn't depend on there being
  // any games installed, and takes priority over every other gesture since
  // releasing either button ends it immediately (see showing_battery_'s
  // derivation in update()).
  if (showing_battery_) {
    renderBatteryGauge(engine);
    return;
  }

  if (delete_confirmation_) {
    renderList(engine);
    drawLauncherSlot(display, deleted_slot_, colors::kRed, 1.0f);
    return;
  }

  if (!deleting_ && delete_hold_ms_ > 0 && selected_ < gameList().gameCount() &&
      selectedEntry().is_installed) {
    renderList(engine);
    return;
  }

  // Holding the nav button past kHighscoreHoldMs shows the selected game's
  // highscore, instantly rather than bit-by-bit, so a quick peek doesn't have
  // to wait out a reveal animation meant for a score just earned.
  if (nav_hold_exceeded_ && selected_ < gameList().gameCount()) {
    engine.renderScore(engine.storage().highscore(selectedEntry().id),
                       engine.input().holdDuration(Input::kNavButton),
                       /*instant=*/true);
    return;
  }

  if (launching_) {
    // The chosen game's colour floods the whole tube, then hands over. It makes
    // the launch feel like a commitment rather than an instant cut.
    const float progress = 1.0f - (launch_timer_ / kLaunchFlashTime);
    display.span(0.0f, progress, selectedEntry().accent, 1.0f);
    return;
  }

  renderList(engine);
}

}  // namespace beamboy
