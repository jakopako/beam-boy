// Beam Boy — firmware entry point.
//
// main.cpp does nothing but wire the pieces together and pump the frame loop;
// all behaviour lives in scenes. Games come from two places, merged into one
// list: the built-in registry, and script cartridges installed under /games/
// on the filesystem (see core/cartridge_store.h).

#include <Arduino.h>
#include <WiFi.h>

#include "core/cartridge_store.h"
#include "core/engine.h"
#include "core/game_registry.h"
#include "scenes/launcher_scene.h"

namespace {

beamboy::Engine engine;
beamboy::LauncherScene launcher_scene;
beamboy::CartridgeStore cartridge_store;

// Frame timing is reported periodically rather than every frame, since serial
// output is slow enough to distort the very measurement it is reporting.
constexpr uint32_t kDiagnosticIntervalMs = 5000;
uint32_t last_diagnostic_ms = 0;

}  // namespace

void setup() {
  Serial.begin(115200);

  // Wait for the USB CDC link, then wait a little longer.
  //
  // On the S3 there is no USB-serial chip: the ESP32 *is* the USB device, so a
  // reset makes it vanish from the bus and re-enumerate. `Serial` goes true the
  // moment the device side of the link is up, which is earlier than the host's
  // monitor manages to reopen the port -- so anything printed immediately after
  // this loop is written into a void and never seen. That is why the boot
  // banner appeared to go missing on this board while the later [PERF] lines
  // came through fine.
  //
  // The extra settle delay is the fix, and it costs nothing: it only applies
  // when a host is actually attached. On a battery-powered console with no USB
  // host, `Serial` never goes true, the loop falls out on the timeout below,
  // and boot is not delayed at all.
  const uint32_t start = millis();
  while (!Serial && (millis() - start < 3000)) {
    delay(10);
  }
  if (Serial) {
    delay(400);
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

  // Discover installed cartridges before the launcher opens. Scanning the
  // filesystem and parsing JSON is slow enough to drop frames, so it happens
  // once here rather than while the launcher is rendering.
  cartridge_store.scan();
  beamboy::gameList().build(cartridge_store);

  engine.setLauncher(&launcher_scene);
  engine.setScene(&launcher_scene);

  Serial.println();
  Serial.println("==================================");
  Serial.println("Beam Boy -- Phase 7: cartridge store");
  Serial.print("Pixels:     ");
  Serial.println(engine.display().pixelCount());
  Serial.print("Brightness: ");
  Serial.println(engine.display().brightness());
  Serial.print("Storage:    ");
  Serial.println(engine.storage().mounted() ? "LittleFS mounted"
                                            : "UNAVAILABLE");
  Serial.print("Games:      ");
  Serial.print(beamboy::gameList().count());
  Serial.print(" (");
  Serial.print(beamboy::games::kGameCount);
  Serial.print(" built in, ");
  Serial.print(cartridge_store.count());
  Serial.println(" installed)");
  for (uint8_t i = 0; i < beamboy::gameList().count(); i++) {
    const beamboy::GameEntry& game = beamboy::gameList().at(i);
    // Mark which entries came off the filesystem, so a cartridge that failed
    // to load is obvious here rather than only in the [games] lines above.
    const bool installed = i >= beamboy::games::kGameCount;
    Serial.print(installed ? "  * " : "  - ");
    Serial.print(game.title);
    Serial.print("  (best ");
    Serial.print(engine.storage().highscore(game.id));
    Serial.println(")");
  }
  Serial.println("            (* = installed cartridge from /games)");
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
