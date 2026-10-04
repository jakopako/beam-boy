#include "scenes/settings_scene.h"

#include <math.h>

#include "core/cartridge_store.h"
#include "scenes/settings_menu.h"

namespace beamboy {
namespace {

constexpr float kHighlightEase = 12.0f;

}  // namespace

void SettingsScene::enter(Engine& engine) {
  (void)engine;
  highlight_ = static_cast<float>(selected_);
}

Scene* SettingsScene::selectedScene() const {
  return games::kUtilities[settings::kItems[selected_]].scene;
}

void SettingsScene::update(Engine& engine, float dt) {
  const int8_t step = engine.input().navDelta();
  if (step != 0) {
    const int16_t next = static_cast<int16_t>(selected_) + step;
    if (next >= 0 && next < settings::kItemCount) {
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

  for (uint8_t i = 0; i < settings::kItemCount; i++) {
    const float position = (static_cast<float>(i) + 1.0f) /
                           (static_cast<float>(settings::kItemCount) + 1.0f);
    const uint8_t utility_index = settings::kItems[i];
    const float distance = fabsf(highlight_ - static_cast<float>(i));
    float intensity = distance < 1.0f ? 0.2f + 0.8f * (1.0f - distance) : 0.2f;
    if (i == selected_) {
      intensity *= 0.75f + 0.25f * pulse(millis() / 1000.0f, 4.0f);
    }
    display.point(position, games::kUtilities[utility_index].accent, intensity);
  }
}

}  // namespace beamboy
