// Beam Boy — firmware entry point.
//
// Phase 3: launcher and persistence. main.cpp does nothing but wire the pieces
// together and pump the frame loop; all behaviour lives in scenes, and the list
// of games lives in the registry.

#include <Arduino.h>
#include <WiFi.h>

#include "core/engine.h"
#include "core/game_registry.h"
#include "scenes/launcher_scene.h"

namespace {

beamboy::Engine engine;
beamboy::LauncherScene launcher_scene;

// Frame timing is reported periodically rather than every frame, since serial
// output is slow enough to distort the very measurement it is reporting.
constexpr uint32_t kDiagnosticIntervalMs = 5000;
uint32_t last_diagnostic_ms = 0;

}  // namespace

void setup() {
  Serial.begin(115200);

  const uint32_t start = millis();
  while (!Serial && (millis() - start < 3000)) {
    delay(10);
  }

  // The radio powers up automatically at boot and draws tens of mA even when
  // idle, several times the LED budget. Beam Boy is offline by default, so keep
  // it off until the user explicitly opts in (Phase 4). Network::begin() does
  // this too, but only runs on Network-scene entry -- this is the guard for
  // every boot before the user ever visits that scene.
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

  // Calibrates the joystick centre, so leave the stick untouched at boot.
  engine.begin();
  engine.setLauncher(&launcher_scene);
  engine.setScene(&launcher_scene);

  Serial.println();
  Serial.println("==================================");
  Serial.println("Beam Boy -- Phase 3 launcher");
  Serial.print("Pixels:     ");
  Serial.println(engine.display().pixelCount());
  Serial.print("Brightness: ");
  Serial.println(engine.display().brightness());
  Serial.print("Storage:    ");
  Serial.println(engine.storage().mounted() ? "LittleFS mounted"
                                            : "UNAVAILABLE");
  Serial.print("Games:      ");
  Serial.println(beamboy::games::kGameCount);
  for (uint8_t i = 0; i < beamboy::games::kGameCount; i++) {
    const beamboy::GameEntry& game = beamboy::games::kGames[i];
    Serial.print("  - ");
    Serial.print(game.title);
    Serial.print("  (best ");
    Serial.print(engine.storage().highscore(game.id));
    Serial.println(")");
  }
  Serial.println();
  Serial.println("Launcher : stick/wheel selects, A or nav-press launches");
  Serial.println("           hold B to see the highscore");
  Serial.println("In game  : nav-press pauses, then hold B to exit");
  Serial.println("==================================");
}

void loop() {
  engine.tick();

  const uint32_t now = millis();
  if (now - last_diagnostic_ms >= kDiagnosticIntervalMs) {
    last_diagnostic_ms = now;

    Serial.print("[PERF] fps=");
    Serial.print(engine.fps(), 1);
    Serial.print("  worst frame=");
    Serial.print(engine.worstFrameUs());
    Serial.print("us / 16667us budget  stick=");
    Serial.print(engine.input().stickX(), 2);
    Serial.print(" (raw ");
    Serial.print(engine.input().rawStickX(), 3);
    Serial.println(")");

    engine.resetDiagnostics();
  }
}
