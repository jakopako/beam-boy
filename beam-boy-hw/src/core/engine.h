#pragma once

// Beam Boy — engine core.
//
// Owns the frame loop, so scenes never busy-wait or call delay(). A scene just
// describes what happens in one tick and what it looks like; pacing, input
// sampling, presenting and instrumentation belong here.
//
// This is deliberately the same shape as the eventual scripted game API: a
// script cartridge will expose init/update/render exactly as a native Scene
// does, so porting a game between the two is mechanical.

#include <Arduino.h>

#include "display.h"
#include "input.h"
#include "storage.h"

namespace beamboy {

class Engine;

class Scene {
 public:
  virtual ~Scene() = default;

  virtual void enter(Engine& engine) { (void)engine; }
  virtual void exit(Engine& engine) { (void)engine; }

  // dt is the fixed timestep in seconds, so physics stays deterministic
  // regardless of how long rendering actually took.
  virtual void update(Engine& engine, float dt) = 0;
  virtual void render(Engine& engine) = 0;

  // Called on frames the engine skips, when the scene has opted in via
  // Engine::setIdleServiced(). Intended for work with a deadline of its own
  // that the 60 Hz frame gate would otherwise starve -- in practice, servicing
  // the WiFi stack.
  //
  // Must stay cheap and non-blocking: it runs far more often than update().
  // Nothing here may draw, since no present() follows it.
  virtual void idle(Engine& engine) { (void)engine; }

  // The score to record when this scene is left. Games that keep score override
  // this; the launcher and other non-game scenes leave it at zero.
  virtual uint32_t score() const { return 0; }
};

class Engine {
 public:
  void begin();

  // Runs one frame if the timestep has elapsed; returns immediately otherwise.
  // Called from loop().
  void tick();

  // Lets the current scene ask to be serviced between frames as well as on
  // them. This is the one sanctioned exception to "nothing may block the frame
  // loop" (PLAN.md §2), and it is deliberately narrow.
  //
  // The rule assumes the frame loop is the only thing with a deadline. That is
  // true for games, and false for the WiFi stack: association, scanning and RF
  // calibration have their own timing requirements enforced in the SDK, and
  // when they are not met the device faults inside the SDK's timing callbacks
  // rather than merely dropping frames. Gating the network's service call to
  // 60 Hz starved exactly that work.
  //
  // Note this does not let a scene *block*. It lets a scene be called more
  // often, in the idle time the frame gate would otherwise spin away. Games
  // still see a fixed timestep and are unaffected.
  void setIdleServiced(bool serviced) { idle_serviced_ = serviced; }

  void setScene(Scene* scene);

  Display& display() { return display_; }
  Input& input() { return input_; }
  Storage& storage() { return storage_; }

  // --- Launcher integration ------------------------------------------------
  //
  // The engine owns the return path to the launcher so that *every* game gets
  // it for free and behaves identically. A game cannot forget to implement it,
  // and a downloadable cartridge cannot refuse to honour it -- which matters
  // once cartridges are community-written.

  void setLauncher(Scene* launcher) { launcher_ = launcher; }

  // Which registry entry is currently running, so scores are filed against the
  // right game id. Negative means "not a game" (the launcher itself).
  void setCurrentGame(int8_t index) { current_game_ = index; }
  int8_t currentGame() const { return current_game_; }

  // Files the running game's score and returns to the launcher.
  void exitToLauncher();

  // True while the exit gesture is being held, and how far through it is (0..1),
  // so games can render the progress. Showing the gesture filling up is what
  // stops it feeling like the console froze.
  bool exitGestureActive() const { return exit_gesture_progress_ > 0.0f; }
  float exitGestureProgress() const { return exit_gesture_progress_; }

  bool paused() const { return paused_; }

  // How long B must be held *while paused* to leave a game. Exit is gated
  // behind the pause because games use long holds during play.
  static constexpr uint32_t kExitHoldMs = 1200;

  // Milliseconds since the current scene was entered.
  uint32_t sceneTime() const { return millis() - scene_started_ms_; }

  // --- Score readout -------------------------------------------------------
  // A 1D display cannot render digits, so scores are shown in binary: one pixel
  // per bit, least significant bit first. Bits are coloured by nibble so place
  // values can be read at a glance rather than counted.
  //
  // Lives in the engine rather than in each game so the presentation stays
  // consistent across cartridges.
  void renderScore(uint32_t score, uint32_t elapsed_ms);
  void drawScoreBit(uint16_t bit, float intensity);
  void renderPauseOverlay();

  // How long a full score reveal animation takes.
  static constexpr uint32_t kScoreRevealMs = 1800;

  // --- Diagnostics ---------------------------------------------------------

  float fps() const { return fps_; }

  // Longest frame seen, in microseconds. The fixed timestep is 16667 us, so a
  // sustained value near that means the frame budget is exhausted -- the number
  // the Phase 5 scripting benchmark depends on.
  uint32_t worstFrameUs() const { return worst_frame_us_; }

  void resetDiagnostics();

 private:
  static constexpr uint32_t kTargetFps = 60;
  static constexpr uint32_t kFrameIntervalUs = 1000000UL / kTargetFps;
  static constexpr float kFixedDt = 1.0f / kTargetFps;

  Display display_;
  Input input_;
  Storage storage_;
  Scene* scene_ = nullptr;
  Scene* pending_scene_ = nullptr;
  Scene* launcher_ = nullptr;
  int8_t current_game_ = -1;
  bool paused_ = false;
  // See setIdleServiced(). When true, tick() calls the scene's idle() on
  // frames it would otherwise skip entirely.
  bool idle_serviced_ = false;
  bool exit_armed_ = false;
  float exit_gesture_progress_ = 0.0f;

  uint32_t last_frame_us_ = 0;
  uint32_t scene_started_ms_ = 0;

  uint32_t worst_frame_us_ = 0;
  uint32_t frames_this_second_ = 0;
  uint32_t fps_window_start_ms_ = 0;
  float fps_ = 0.0f;
};

}  // namespace beamboy
