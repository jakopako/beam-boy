#include "scenes/launcher_scene.h"

#include <math.h>

#include "core/game_registry.h"

namespace beamboy {
namespace {

constexpr float kHighlightEase = 12.0f;
constexpr float kLaunchFlashTime = 0.35f;

// A game needs at least this many pixels to read as a block rather than a dot.
constexpr uint8_t kMinBlockPixels = 2;
constexpr uint8_t kMaxBlockPixels = 6;

// Gap between blocks, so adjacent games of similar colour stay distinct.
constexpr uint8_t kGapPixels = 1;

}  // namespace

void LauncherScene::enter(Engine& engine) {
  // Resume on whatever was played last: the console picks up where it was.
  selected_ = engine.storage().lastGame();
  if (selected_ >= games::kGameCount) selected_ = 0;

  highlight_ = static_cast<float>(selected_);
  scroll_ = 0;
  launching_ = false;
  launch_timer_ = 0.0f;

  engine.setCurrentGame(-1);
  engine.display().clear();
}

uint8_t LauncherScene::blockPixels(const Display& display) const {
  if (games::kGameCount == 0) return kMinBlockPixels;

  // Fit every game if we can; only shrink blocks down to the readable minimum.
  const uint16_t available = display.pixelCount();
  const uint16_t per_game = available / games::kGameCount;

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
      Scene* scene = games::kGames[selected_].scene;
      if (scene != nullptr) {
        engine.setCurrentGame(static_cast<int8_t>(selected_));
        engine.setScene(scene);
      } else {
        launching_ = false;
      }
    }
    return;
  }

  if (games::kGameCount == 0) return;

  // Navigation comes through navDelta(), not the raw stick, so this code is
  // identical once the rotary encoder is fitted.
  const int8_t step = input.navDelta();
  if (step != 0) {
    const int16_t next = static_cast<int16_t>(selected_) + step;
    // Clamp rather than wrap: on a physical line, running off the end and
    // reappearing at the other is disorienting.
    if (next >= 0 && next < static_cast<int16_t>(games::kGameCount)) {
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

  highlight_ += (static_cast<float>(selected_) - highlight_) * kHighlightEase * dt;

  if (input.pressed(Button::kA) || input.pressed(Input::kNavButton)) {
    launching_ = true;
    launch_timer_ = kLaunchFlashTime;
  }
}

void LauncherScene::renderList(Engine& engine) {
  Display& display = engine.display();

  const uint8_t block = blockPixels(display);
  const uint8_t stride = block + kGapPixels;
  const uint8_t slots = visibleSlots(display);

  for (uint8_t slot = 0; slot < slots; slot++) {
    const uint16_t index = scroll_ + slot;
    if (index >= games::kGameCount) break;

    const GameEntry& game = games::kGames[index];

    // Distance from the (eased) highlight drives brightness, so the selection
    // reads as a glow moving along the line rather than a discrete jump.
    const float distance = fabsf(highlight_ - static_cast<float>(index));
    float intensity;
    if (distance < 1.0f) {
      // Selected: breathing, so it is unmistakable even among similar colours.
      const float pulse = 0.75f + 0.25f * sinf(millis() / 1000.0f * 4.0f);
      intensity = (0.22f + 0.78f * (1.0f - distance)) * pulse;
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
  if (scroll_ + slots < games::kGameCount) {
    display.rawPixel(display.pixelCount() - 1, Color(60, 60, 60));
  }
}

void LauncherScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  if (games::kGameCount == 0) {
    // Nothing installed: a slow red pulse rather than a dark, dead-looking tube.
    const float pulse = 0.3f + 0.3f * sinf(millis() / 1000.0f * 2.0f);
    display.point(0.5f, colors::kRed, pulse);
    return;
  }

  // Holding B shows the selected game's highscore, so records are visible from
  // the menu without launching anything.
  if (engine.input().held(Button::kB)) {
    engine.renderScore(engine.storage().highscore(games::kGames[selected_].id),
                       engine.input().holdDuration(Button::kB));
    return;
  }

  if (launching_) {
    // The chosen game's colour floods the whole tube, then hands over. It makes
    // the launch feel like a commitment rather than an instant cut.
    const float progress = 1.0f - (launch_timer_ / kLaunchFlashTime);
    display.span(0.0f, progress, games::kGames[selected_].accent, 1.0f);
    return;
  }

  renderList(engine);
}

}  // namespace beamboy
