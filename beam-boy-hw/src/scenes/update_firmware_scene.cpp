#include "scenes/update_firmware_scene.h"

namespace beamboy {

void UpdateFirmwareScene::enter(Engine& engine) {
  (void)engine;
  Serial.println("[update] not implemented yet; hold B to return to Settings");
}

void UpdateFirmwareScene::update(Engine& engine, float dt) {
  (void)dt;
  if (engine.input().pressed(Button::kA)) {
    Serial.println("[update] unavailable; no firmware update was started");
  }
}

void UpdateFirmwareScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  // Two separated amber bars distinguish "unavailable" from download progress.
  const float level = 0.45f + 0.15f * pulse(engine.sceneTime() / 1000.0f, 3.0f);
  display.span(0.1f, 0.3f, colors::kAmber, level);
  display.span(0.7f, 0.9f, colors::kAmber, level);
}

}  // namespace beamboy
