#pragma once

// Beam Boy — Settings > Update firmware.
//
// Entering is the player's explicit opt-in to turn the radio on and ask
// whether a newer signed firmware exists; it never writes flash by itself.
// Installing needs a deliberate 1.2 s hold of A, a fresh battery reading of at
// least 30% and clean saves. After success, a new A press restarts into the
// new firmware; hold B leaves without restarting, and the new image still runs
// at the next restart.
//
// The decisions live in update_flow.h and the visuals in
// update_firmware_view.h, both host-tested. This class only connects them to
// Network, Ota, Power and Storage.

#include "core/engine.h"
#include "core/network.h"
#include "core/ota.h"
#include "scenes/update_flow.h"

namespace beamboy {

class UpdateFirmwareScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void exit(Engine& engine) override;
  void idle(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;
  void abortForShutdown(Engine& engine) override;

 private:
  void start();
  void runCheck(Engine& engine);
  void runInstall(Engine& engine);
  void reboot(Engine& engine);
  void stopEverything(Engine& engine);
  void paint(Engine& engine, update::Stage stage);
  void logStageChange();

  Network net_;
  Ota ota_;
  update::Flow flow_;
  update::Stage logged_stage_ = update::Stage::kNoCredentials;
  bool logged_any_ = false;
};

}  // namespace beamboy
