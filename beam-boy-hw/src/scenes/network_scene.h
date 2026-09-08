#pragma once

// Beam Boy — network scene.
//
// The only place in the firmware that switches the radio on, so the "offline by
// default" promise is enforced by structure rather than by discipline: if you
// never open this scene, the radio never comes up.
//
// Everything the user learns about the network, they learn from a one-
// dimensional line of light. There is no text, so each state gets a distinct
// *motion* as well as a distinct colour -- colour alone is not enough, both
// because the tube's dim end quantises hue badly and because a red/green split
// is invisible to a red-green colourblind player.

#include "../core/engine.h"
#include "../core/network.h"
#include "../core/ota.h"

namespace beamboy {

class NetworkScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void exit(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;
  // Services the WiFi stack between frames; see Engine::setIdleServiced().
  void idle(Engine& engine) override;

 private:
  // What the scene is offering, which depends on whether we have credentials.
  enum class Menu : uint8_t {
    kConnect,  // Use stored credentials.
    kSetup,    // Open the portal to enter new ones.
    kForget,   // Erase stored credentials.
  };

  void applyMenu(Engine& engine);
  void drawMenu(Engine& engine);
  void drawStatus(Engine& engine);
  void drawOta(Engine& engine);

  Network net_;
  Ota ota_;
  Menu menu_ = Menu::kConnect;
  float phase_ = 0.0f;

  // Forget is a destructive action reached by a single button press on a console
  // with no text, so it asks first: the second press within the window confirms.
  // Nothing else in the menu can be triggered by accident in a way the user
  // cannot immediately undo, so nothing else needs this.
  bool forget_armed_ = false;
  uint32_t forget_armed_at_ms_ = 0;
  static constexpr uint32_t kForgetArmMs = 3000;

  // Latches the moment credentials were erased, so the tube can acknowledge it.
  // Without this, a successful forget looks identical to a button that did
  // nothing -- the menu simply has one fewer entry afterwards.
  uint32_t forgot_at_ms_ = 0;
  static constexpr uint32_t kForgotFlashMs = 1200;

  // Once connected, B offers the firmware update rather than disconnecting.
  // Kept separate from the menu because it is only reachable from a connected
  // state, and because an accidental firmware install is worth one extra step.
  bool ota_active_ = false;

  // Latches the moment a terminal state was reached, so success and failure can
  // flash briefly and then settle rather than strobing forever.
  uint32_t settled_at_ms_ = 0;
  NetState last_state_ = NetState::kOff;
};

}  // namespace beamboy
