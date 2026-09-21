#include "scenes/settings_scene.h"

#include <math.h>

#include "core/cartridge_store.h"

namespace beamboy {
namespace {

constexpr uint8_t kItemCount = 2;
constexpr float kHighlightEase = 12.0f;

}  // namespace

void SettingsScene::enter(Engine& engine) {
  (void)engine;
  highlight_ = static_cast<float>(selected_);
}

Scene* SettingsScene::selectedScene() const {
  const uint8_t utility_index =
      selected_ == 0 ? games::kNetworkUtilityIndex : games::kStoreUtilityIndex;
  return games::kUtilities[utility_index].scene;
}

void SettingsScene::update(Engine& engine, float dt) {
  const int8_t step = engine.input().navDelta();
  if (step != 0) {
    const int16_t next = static_cast<int16_t>(selected_) + step;
    if (next >= 0 && next < kItemCount) {
      selected_ = static_cast<uint8_t>(next);
    }
  }

  highlight_ +=
      (static_cast<float>(selected_) - highlight_) * kHighlightEase * dt;

  if (!engine.input().pressed(Button::kA)) return;

  Scene* scene = selectedScene();
  const int8_t index = gameList().indexOf(scene);
  if (scene == nullptr || index < 0) {
    Serial.println("[settings] selected utility is unavailable");
    return;
  }

  engine.setUtilityReturn(this);
  engine.setCurrentGame(index);
  engine.setScene(scene);
}

void SettingsScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  // Two evenly-spaced items for the compatibility bridge. Point 2 will replace
  // these with the final four-item settings layout.
  for (uint8_t i = 0; i < kItemCount; i++) {
    const float position = i == 0 ? 0.32f : 0.68f;
    const uint8_t utility_index =
        i == 0 ? games::kNetworkUtilityIndex : games::kStoreUtilityIndex;
    const float distance = fabsf(highlight_ - static_cast<float>(i));
    float intensity =
        distance < 1.0f ? 0.2f + 0.8f * (1.0f - distance) : 0.2f;
    if (i == selected_) {
      intensity *=
          0.75f + 0.25f * pulse(millis() / 1000.0f, 4.0f);
    }
    display.point(position, games::kUtilities[utility_index].accent, intensity);
  }
}

}  // namespace beamboy
