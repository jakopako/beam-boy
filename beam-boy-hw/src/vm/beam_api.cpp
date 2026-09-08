#include "vm/beam_api.h"

#include <string.h>

#include "core/cartridge_store.h"
#include "core/game_registry.h"

namespace beamboy {
namespace {

BeamApiContext* g_ctx = nullptr;

Engine& engine() { return *g_ctx->engine; }

// Colors cross the VM boundary as a packed 0xRRGGBB int, per PLAN.md's
// cartridge API sketch -- one int argument rather than three, and it matches
// the literal form a script author would actually write (0xff8000, not three
// separate numbers).
Color colorFromPacked(int32_t packed) {
  return Color(static_cast<uint8_t>((packed >> 16) & 0xFF),
               static_cast<uint8_t>((packed >> 8) & 0xFF),
               static_cast<uint8_t>(packed & 0xFF));
}

float argReal(bvm* vm, int index, float fallback = 0.0f) {
  if (index > be_top(vm) || !be_isnumber(vm, index)) return fallback;
  return static_cast<float>(be_toreal(vm, index));
}

int32_t argInt(bvm* vm, int index, int32_t fallback = 0) {
  if (index > be_top(vm) || !be_isnumber(vm, index)) return fallback;
  return static_cast<int32_t>(be_toint(vm, index));
}

Button argButton(bvm* vm, int index) {
  if (index > be_top(vm) || !be_isstring(vm, index)) return Button::kCount;
  const char* name = be_tostring(vm, index);
  if (strcmp(name, "a") == 0) return Button::kA;
  if (strcmp(name, "b") == 0) return Button::kB;
  if (strcmp(name, "stick") == 0) return Button::kStick;
  return Button::kCount;
}

// --- Drawing ---------------------------------------------------------------

int beam_clear(bvm* vm) {
  engine().display().clear();
  be_return_nil(vm);
}

int beam_pixel(bvm* vm) {
  const float pos = argReal(vm, 1);
  const Color color = colorFromPacked(argInt(vm, 2));
  const float intensity = argReal(vm, 3, 1.0f);
  engine().display().point(pos, color, intensity);
  be_return_nil(vm);
}

int beam_line(bvm* vm) {
  const float from = argReal(vm, 1);
  const float to = argReal(vm, 2);
  const Color color = colorFromPacked(argInt(vm, 3));
  const float intensity = argReal(vm, 4, 1.0f);
  engine().display().span(from, to, color, intensity);
  be_return_nil(vm);
}

int beam_fade(bvm* vm) {
  engine().display().fade(argReal(vm, 1, 1.0f));
  be_return_nil(vm);
}

int beam_pixel_count(bvm* vm) {
  be_pushint(vm, engine().display().pixelCount());
  be_return(vm);
}

int beam_pixel_width(bvm* vm) {
  be_pushreal(vm, engine().display().pixelWidth());
  be_return(vm);
}

// --- Input -------------------------------------------------------------

int beam_wheel(bvm* vm) {
  be_pushint(vm, engine().input().navDelta());
  be_return(vm);
}

int beam_stick(bvm* vm) {
  be_pushreal(vm, engine().input().stickX());
  be_return(vm);
}

int beam_pressed(bvm* vm) {
  const Button button = argButton(vm, 1);
  be_pushbool(vm, button != Button::kCount && engine().input().pressed(button));
  be_return(vm);
}

int beam_held(bvm* vm) {
  const Button button = argButton(vm, 1);
  be_pushbool(vm, button != Button::kCount && engine().input().held(button));
  be_return(vm);
}

// --- Timing / misc -----------------------------------------------------

int beam_time(bvm* vm) {
  be_pushint(vm, static_cast<int32_t>(engine().sceneTime()));
  be_return(vm);
}

// Inclusive of both ends, matching Lua/Berry's usual random(m, n) convention
// and PLAN.md's beam.random(n) sketch generalised to a range.
int beam_random(bvm* vm) {
  const int argc = be_top(vm);
  int32_t result;
  if (argc >= 2) {
    result = random(argInt(vm, 1), argInt(vm, 2) + 1);
  } else {
    result = random(0, argInt(vm, 1, 1) + 1);
  }
  be_pushint(vm, result);
  be_return(vm);
}

// --- Score / persistence ------------------------------------------------
//
// A cartridge never touches Storage directly. beam.score(add) accumulates a
// running total the engine files under the running game's id when the script
// exits (see ScriptScene::exit) -- exactly the same "engine submits, game
// cannot forget or lie" guarantee native games already have via
// Engine::exitToLauncher().

int beam_score(bvm* vm) {
  const int argc = be_top(vm);
  if (argc >= 1 && be_isnumber(vm, 1)) {
    const int32_t delta = argInt(vm, 1);
    if (delta > 0) g_ctx->score += static_cast<uint32_t>(delta);
  }
  be_pushint(vm, static_cast<int32_t>(g_ctx->score));
  be_return(vm);
}

int beam_highscore(bvm* vm) {
  // Scripts read only their own game's highscore, looked up by the id the
  // engine already associates with the running scene -- there is no
  // cross-game read, so a cartridge cannot snoop another game's score.
  const int8_t index = engine().currentGame();
  const char* game_id =
      (index >= 0 && index < static_cast<int8_t>(gameList().count()))
          ? gameList().at(index).id
          : nullptr;
  be_pushint(vm, static_cast<int32_t>(
                     game_id ? engine().storage().highscore(game_id) : 0));
  be_return(vm);
}

int beam_exit(bvm* vm) {
  g_ctx->exit_requested = true;
  be_return_nil(vm);
}

// The engine-drawn binary score reveal (see Engine::renderScore) -- kept out
// of scripts entirely so every cartridge's score screen looks the same, per
// PLAN.md's "put this in the engine, not in each game" note.
int beam_show_score(bvm* vm) {
  const uint32_t elapsed_ms = static_cast<uint32_t>(argInt(vm, 1, 0));
  engine().renderScore(g_ctx->score, elapsed_ms);
  be_return_nil(vm);
}

// Bypasses the normalised coordinate space to address one pixel by index --
// used for small engine-style chrome like life indicators, matching
// Display::rawPixel's own "not for games, but for UI" framing. Exposed to
// scripts anyway (unlike rawPixel's C++ comment suggests) since a scripted
// cartridge has no other way to draw fixed UI at the far end of the strip.
int beam_raw_pixel(bvm* vm) {
  const uint16_t index = static_cast<uint16_t>(argInt(vm, 1, 0));
  const Color color = colorFromPacked(argInt(vm, 2));
  const float intensity = argReal(vm, 3, 1.0f);
  engine().display().rawPixel(index, color.scaled(intensity));
  be_return_nil(vm);
}

int beam_log(bvm* vm) {
  if (be_top(vm) >= 1) {
    Serial.print("[be] ");
    Serial.println(be_tostring(vm, 1));
  }
  be_return_nil(vm);
}

}  // namespace

void setBeamApiContext(BeamApiContext* ctx) { g_ctx = ctx; }

BeamApiContext* beamApiContext() { return g_ctx; }

void bindBeamApi(bvm* vm) {
  be_newmodule(vm);

  be_pushntvfunction(vm, beam_clear);
  be_setmember(vm, -2, "clear");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_pixel);
  be_setmember(vm, -2, "pixel");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_line);
  be_setmember(vm, -2, "line");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_fade);
  be_setmember(vm, -2, "fade");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_pixel_count);
  be_setmember(vm, -2, "pixel_count");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_pixel_width);
  be_setmember(vm, -2, "pixel_width");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_wheel);
  be_setmember(vm, -2, "wheel");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_stick);
  be_setmember(vm, -2, "stick");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_pressed);
  be_setmember(vm, -2, "pressed");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_held);
  be_setmember(vm, -2, "held");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_time);
  be_setmember(vm, -2, "time");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_random);
  be_setmember(vm, -2, "random");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_score);
  be_setmember(vm, -2, "score");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_highscore);
  be_setmember(vm, -2, "highscore");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_exit);
  be_setmember(vm, -2, "exit");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_show_score);
  be_setmember(vm, -2, "show_score");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_raw_pixel);
  be_setmember(vm, -2, "raw_pixel");
  be_pop(vm, 1);

  be_pushntvfunction(vm, beam_log);
  be_setmember(vm, -2, "log");
  be_pop(vm, 1);

  be_setglobal(vm, "beam");
  be_pop(vm, 1);
}

}  // namespace beamboy
