#include "scenes/launcher_scene.h"

#include <math.h>

#include "core/cartridge_store.h"
#include "core/game_registry.h"

namespace beamboy {
namespace {

constexpr float kHighlightEase = 12.0f;
constexpr float kLaunchFlashTime = 0.35f;

// How long B must be held on an installed cartridge, beyond the highscore
// readout, before it is deleted. Long enough that showing the highscore (the
// gesture's first, harmless outcome) is never mistaken for the start of a
// delete; short enough that deleting a game does not feel like a chore.
constexpr uint32_t kDeleteHoldMs = 2500;

// A game needs at least this many pixels to read as a block rather than a dot.
constexpr uint8_t kMinBlockPixels = 2;
constexpr uint8_t kMaxBlockPixels = 6;

// Gap between blocks, so adjacent games of similar colour stay distinct.
constexpr uint8_t kGapPixels = 1;

}  // namespace

void LauncherScene::enter(Engine& engine) {
  // Resume on whatever was played last: the console picks up where it was.
  selected_ = engine.storage().lastGame();
  if (selected_ >= gameList().count()) selected_ = 0;

  highlight_ = static_cast<float>(selected_);
  scroll_ = 0;
  launching_ = false;
  launch_timer_ = 0.0f;

  engine.setCurrentGame(-1);
  engine.display().clear();
}

uint8_t LauncherScene::blockPixels(const Display& display) const {
  if (gameList().count() == 0) return kMinBlockPixels;

  // Fit every game if we can; only shrink blocks down to the readable minimum.
  const uint16_t available = display.pixelCount();
  const uint16_t per_game = available / gameList().count();

  if (per_game <= kMinBlockPixels + kGapPixels) return kMinBlockPixels;

  uint8_t block = static_cast<uint8_t>(per_game - kGapPixels);
  if (block > kMaxBlockPixels) block = kMaxBlockPixels;
  return block;
}

uint8_t LauncherScene::visibleSlots(const Display& display) const {
  const uint8_t stride = blockPixels(display) + kGapPixels;
  if (stride == 0) return 1;
  const uint8_t slots = static_cast<uint8_t>(display.pixelCount() / stride);
  return slots < 1 ? 1 : slots;
}

void LauncherScene::update(Engine& engine, float dt) {
  Input& input = engine.input();
  Display& display = engine.display();

  if (launching_) {
    launch_timer_ -= dt;
    if (launch_timer_ <= 0.0f) {
      Scene* scene = gameList().at(selected_).scene;
      if (scene != nullptr) {
        engine.setCurrentGame(static_cast<int8_t>(selected_));
        engine.setScene(scene);
      } else {
        launching_ = false;
      }
    }
    return;
  }

  if (gameList().count() == 0) return;

  // Navigation comes through navDelta(), not the raw stick, so this code is
  // identical once the rotary encoder is fitted.
  const int8_t step = input.navDelta();
  if (step != 0) {
    const int16_t next = static_cast<int16_t>(selected_) + step;
    // Clamp rather than wrap: on a physical line, running off the end and
    // reappearing at the other is disorienting.
    if (next >= 0 && next < static_cast<int16_t>(gameList().count())) {
      selected_ = static_cast<uint8_t>(next);
    }
  }

  // Keep the selection on screen, scrolling only when it would fall off.
  const uint8_t slots = visibleSlots(display);
  if (selected_ < scroll_) {
    scroll_ = selected_;
  } else if (selected_ >= scroll_ + slots) {
    scroll_ = selected_ - slots + 1;
  }

  highlight_ +=
      (static_cast<float>(selected_) - highlight_) * kHighlightEase * dt;

  if (input.pressed(Button::kA) || input.pressed(Input::kNavButton)) {
    launching_ = true;
    launch_timer_ = kLaunchFlashTime;
  }

  // Deleting only applies to installed cartridges -- a built-in or a utility
  // scene (Store, Network) must never disappear from the launcher this way.
  if (!deleting_ && gameList().at(selected_).is_installed &&
      input.heldFor(Button::kB, kDeleteHoldMs)) {
    deleting_ = true;
    CartridgeStore::remove(gameList().at(selected_).id);
    rescan_store_.scan();
    gameList().build(rescan_store_);
    // The list just shrank; clamp rather than let selected_ point past the
    // end or land on a different game than the player expects to see next.
    if (selected_ >= gameList().count()) {
      selected_ = gameList().count() == 0 ? 0 : gameList().count() - 1;
    }
    highlight_ = static_cast<float>(selected_);
  } else if (!input.held(Button::kB)) {
    deleting_ = false;
  }
}

void LauncherScene::renderList(Engine& engine) {
  Display& display = engine.display();

  const uint8_t block = blockPixels(display);
  const uint8_t stride = block + kGapPixels;
  const uint8_t slots = visibleSlots(display);

  for (uint8_t slot = 0; slot < slots; slot++) {
    const uint16_t index = scroll_ + slot;
    if (index >= gameList().count()) break;

    const GameEntry& game = gameList().at(index);

    // Distance from the (eased) highlight drives brightness, so the selection
    // reads as a glow moving along the line rather than a discrete jump.
    const float distance = fabsf(highlight_ - static_cast<float>(index));
    float intensity;
    if (distance < 1.0f) {
      // Selected: breathing, so it is unmistakable even among similar colours.
      const float level = 0.75f + 0.25f * pulse(millis() / 1000.0f, 4.0f);
      intensity = (0.22f + 0.78f * (1.0f - distance)) * level;
    } else {
      intensity = 0.22f;
    }

    const uint16_t start = slot * stride;
    for (uint8_t p = 0; p < block; p++) {
      const uint16_t pixel = start + p;
      if (pixel >= display.pixelCount()) break;
      display.rawPixel(pixel, game.accent.scaled(intensity));
    }
  }

  // Scroll hints: a dim white pixel at either end when the list continues.
  if (scroll_ > 0) {
    display.rawPixel(0, Color(60, 60, 60));
  }
  if (scroll_ + slots < gameList().count()) {
    display.rawPixel(display.pixelCount() - 1, Color(60, 60, 60));
  }
}

void LauncherScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  if (gameList().count() == 0) {
    // Nothing installed: a slow red pulse rather than a dark, dead-looking
    // tube.
    const float level = 0.3f + 0.3f * pulse(millis() / 1000.0f, 2.0f);
    display.point(0.5f, colors::kRed, level);
    return;
  }

  // Holding B shows the selected game's highscore, so records are visible from
  // the menu without launching anything. Once the hold has crossed the delete
  // threshold (see update()), the game is already gone -- show a solid red
  // confirmation instead of a score that no longer belongs to anything
  // selected_ still points at.
  if (deleting_) {
    display.span(0.0f, 1.0f, colors::kRed, 1.0f);
    return;
  }

  if (engine.input().held(Button::kB)) {
    // As the hold nears the delete threshold on a deletable entry, bleed the
    // readout toward red so the countdown is visible before it fires --
    // otherwise deleting a game would look instantaneous and accidental.
    if (gameList().at(selected_).is_installed) {
      const float warn =
          static_cast<float>(engine.input().holdDuration(Button::kB)) /
          static_cast<float>(kDeleteHoldMs);
      if (warn > 0.5f) {
        const float mix = (warn - 0.5f) / 0.5f;
        display.span(0.0f, 1.0f, colors::kRed, mix > 1.0f ? 1.0f : mix);
        return;
      }
    }
    engine.renderScore(engine.storage().highscore(gameList().at(selected_).id),
                       engine.input().holdDuration(Button::kB));
    return;
  }

  if (launching_) {
    // The chosen game's colour floods the whole tube, then hands over. It makes
    // the launch feel like a commitment rather than an instant cut.
    const float progress = 1.0f - (launch_timer_ / kLaunchFlashTime);
    display.span(0.0f, progress, gameList().at(selected_).accent, 1.0f);
    return;
  }

  renderList(engine);
}

}  // namespace beamboy
