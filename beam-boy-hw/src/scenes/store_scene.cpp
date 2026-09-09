#include "scenes/store_scene.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <string.h>

#include "core/cartridge_store.h"
#include "core/game_registry.h"

#ifndef BEAMBOY_STORE_INDEX_URL
#define BEAMBOY_STORE_INDEX_URL \
  "https://jakopako.github.io/beam-boy/games/index.json"
#endif

#ifndef BEAMBOY_STORE_ALLOW_INSECURE_INDEX
#define BEAMBOY_STORE_ALLOW_INSECURE_INDEX 0
#endif

extern const uint8_t rootca_crt_bundle_start[] asm(
    "_binary_cert_x509_crt_bundle_bin_start");

namespace beamboy {
namespace {

constexpr size_t kMaxIndexBytes = 8 * 1024;
constexpr uint32_t kHttpTimeoutMs = 15000;

const Color kConnectColor(0, 160, 255);
const Color kFetchColor(255, 140, 0);
const Color kInstallColor(255, 255, 255);
const Color kOkColor(0, 255, 60);
const Color kFailColor(255, 30, 20);

bool isHttps(const char* url) { return strncmp(url, "https://", 8) == 0; }

bool openTrustedIndexHttp(HTTPClient& http, WiFiClient& plain,
                          WiFiClientSecure& secure, const char* url,
                          const char** error) {
#if BEAMBOY_STORE_ALLOW_INSECURE_INDEX
  if (isHttps(url)) {
    Serial.println(F("[store] WARNING: index TLS verification disabled"));
    secure.setInsecure();
    return http.begin(secure, url);
  }
  Serial.println(F("[store] WARNING: fetching unsigned index over HTTP"));
  return http.begin(plain, url);
#else
  if (!isHttps(url)) {
    *error = "index must use https";
    return false;
  }
  secure.setCACertBundle(rootca_crt_bundle_start);
  return http.begin(secure, url);
#endif
}

bool openScriptHttp(HTTPClient& http, WiFiClient& plain,
                    WiFiClientSecure& secure, const char* url) {
  if (isHttps(url)) {
    secure.setInsecure();
    return http.begin(secure, url);
  }
  return http.begin(plain, url);
}

bool readHttpBody(HTTPClient& http, uint8_t* buffer, size_t expected,
                  size_t& out_read) {
  WiFiClient* stream = http.getStreamPtr();
  out_read = 0;
  uint32_t last_progress_ms = millis();

  while (out_read < expected) {
    const int available = stream->available();
    if (available > 0) {
      const size_t want = expected - out_read;
      const size_t chunk =
          available < static_cast<int>(want) ? available : want;
      const int read = stream->readBytes(buffer + out_read, chunk);
      if (read <= 0) return false;
      out_read += static_cast<size_t>(read);
      last_progress_ms = millis();
      continue;
    }

    if (millis() - last_progress_ms > kHttpTimeoutMs) return false;
    delay(1);
  }

  return true;
}

char* downloadText(const char* url, size_t max_bytes, const char** error) {
  if (WiFi.status() != WL_CONNECTED) {
    *error = "no network";
    return nullptr;
  }

  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);

  if (!openTrustedIndexHttp(http, plain, secure, url, error)) {
    if ((*error)[0] == '\0') *error = "bad index url";
    return nullptr;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    *error = code < 0 ? "connect failed" : "http error";
    return nullptr;
  }

  const int length = http.getSize();
  if (length <= 0 || static_cast<size_t>(length) > max_bytes) {
    http.end();
    *error = "bad length";
    return nullptr;
  }

  char* body = static_cast<char*>(malloc(static_cast<size_t>(length) + 1));
  if (body == nullptr) {
    http.end();
    *error = "out of memory";
    return nullptr;
  }

  size_t read = 0;
  const bool ok = readHttpBody(http, reinterpret_cast<uint8_t*>(body),
                               static_cast<size_t>(length), read) &&
                  read == static_cast<size_t>(length);
  http.end();

  if (!ok) {
    free(body);
    *error = "read failed";
    return nullptr;
  }

  body[length] = '\0';
  return body;
}

bool hexNibble(char c, uint8_t& out) {
  if (c >= '0' && c <= '9') {
    out = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    out = static_cast<uint8_t>(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    out = static_cast<uint8_t>(c - 'A' + 10);
    return true;
  }
  return false;
}

bool shaMatches(const uint8_t* digest, const char* expected_hex) {
  for (uint8_t i = 0; i < 32; i++) {
    uint8_t high;
    uint8_t low;
    if (!hexNibble(expected_hex[i * 2], high) ||
        !hexNibble(expected_hex[i * 2 + 1], low)) {
      return false;
    }
    if (digest[i] != static_cast<uint8_t>((high << 4) | low)) return false;
  }
  return true;
}

bool collidesWithBuiltIn(const char* id) {
  for (uint8_t i = 0; i < games::kGameCount; i++) {
    if (strcmp(id, games::kGames[i].id) == 0) return true;
  }
  for (uint8_t i = 0; i < games::kUtilityCount; i++) {
    if (strcmp(id, games::kUtilities[i].id) == 0) return true;
  }
  return false;
}

bool installedGameVisible(const char* id) {
  for (uint8_t i = games::kGameCount; i < gameList().count(); i++) {
    if (strcmp(gameList().at(i).id, id) == 0) return true;
  }
  return false;
}

bool writeJsonString(File& file, const char* value) {
  if (file.print('"') != 1) return false;
  for (const char* p = value; *p != '\0'; p++) {
    if (*p == '"' || *p == '\\') {
      if (file.print('\\') != 1) return false;
    } else if (*p == '\n') {
      if (file.print("\\n") != 2) return false;
      continue;
    } else if (*p == '\r') {
      if (file.print("\\r") != 2) return false;
      continue;
    } else if (*p == '\t') {
      if (file.print("\\t") != 2) return false;
      continue;
    } else if (static_cast<unsigned char>(*p) < 32) {
      return false;
    }
    if (file.print(*p) != 1) return false;
  }
  return file.print('"') == 1;
}

bool writeMeta(const StoreIndex::Entry& entry, const char* path) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;

  bool ok = true;
  ok = ok && file.print("{\n  \"id\": ") == 10;
  ok = ok && writeJsonString(file, entry.id);
  ok = ok && file.print(",\n  \"title\": ") == 13;
  ok = ok && writeJsonString(file, entry.title);
  ok = ok && file.print(",\n  \"color\": ") == 13;
  ok = ok && writeJsonString(file, entry.color);
  ok = ok && file.print("\n}\n") == 3;
  file.close();
  return ok;
}

bool downloadScript(const StoreIndex::Entry& entry, const char* path,
                    const char** error) {
  if (WiFi.status() != WL_CONNECTED) {
    *error = "no network";
    return false;
  }

  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);

  if (!openScriptHttp(http, plain, secure, entry.url)) {
    *error = "bad game url";
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    *error = code < 0 ? "connect failed" : "game http error";
    return false;
  }

  const int length = http.getSize();
  if (length >= 0 && static_cast<size_t>(length) != entry.size) {
    http.end();
    *error = "size mismatch";
    return false;
  }

  File file = LittleFS.open(path, "w");
  if (!file) {
    http.end();
    *error = "cannot write game";
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[256];
  size_t total = 0;
  uint32_t last_progress_ms = millis();
  bool ok = true;

  while (total < entry.size) {
    const int available = stream->available();
    if (available > 0) {
      const size_t remaining = entry.size - total;
      size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
      if (static_cast<size_t>(available) < want) {
        want = static_cast<size_t>(available);
      }
      const int read = stream->readBytes(buffer, want);
      if (read <= 0) {
        ok = false;
        break;
      }
      if (file.write(buffer, read) != static_cast<size_t>(read)) {
        ok = false;
        break;
      }
      mbedtls_sha256_update_ret(&sha, buffer, static_cast<size_t>(read));
      total += static_cast<size_t>(read);
      last_progress_ms = millis();
      continue;
    }

    if (millis() - last_progress_ms > kHttpTimeoutMs) {
      ok = false;
      break;
    }
    delay(1);
  }

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);
  file.close();
  http.end();

  if (!ok || total != entry.size) {
    LittleFS.remove(path);
    *error = "game read failed";
    return false;
  }

  if (!shaMatches(digest, entry.sha256)) {
    LittleFS.remove(path);
    *error = "hash mismatch";
    return false;
  }

  return true;
}

}  // namespace

void StoreScene::enter(Engine& engine) {
  net_.begin();
  engine.setIdleServiced(true);
  state_ = StoreState::kConnecting;
  selected_ = 0;
  phase_ = 0.0f;
  settled_at_ms_ = 0;
  error_ = "";

  if (!net_.hasCredentials()) {
    fail("no credentials");
    Serial.println(F("[store] no stored WiFi credentials; use Network first"));
    return;
  }

  Serial.println(F("[store] connecting"));
  net_.connect();
}

void StoreScene::exit(Engine& engine) {
  (void)engine;
  net_.disconnect();
}

void StoreScene::idle(Engine& engine) {
  (void)engine;
  net_.tick();
}

void StoreScene::fail(const char* reason) {
  error_ = reason;
  state_ = StoreState::kFailed;
  settled_at_ms_ = millis();
  Serial.print(F("[store] failed: "));
  Serial.println(reason);
}

bool StoreScene::fetchIndex() {
  state_ = StoreState::kFetching;
  Serial.print(F("[store] fetching "));
  Serial.println(BEAMBOY_STORE_INDEX_URL);

  const char* error = "";
  char* body = downloadText(BEAMBOY_STORE_INDEX_URL, kMaxIndexBytes, &error);
  if (body == nullptr) {
    fail(error);
    return false;
  }

  const bool ok = index_.parse(body);
  free(body);

  if (!ok) {
    fail("bad index");
    return false;
  }

  selected_ = 0;
  state_ = StoreState::kReady;
  Serial.print(F("[store] entries: "));
  Serial.println(index_.count());
  for (uint8_t i = 0; i < index_.count(); i++) {
    Serial.print(F("  - "));
    Serial.print(index_.at(i).title);
    Serial.print(F(" ("));
    Serial.print(index_.at(i).id);
    Serial.println(F(")"));
  }
  return true;
}

bool StoreScene::installSelected(Engine& engine) {
  if (index_.count() == 0 || selected_ >= index_.count()) return false;
  if (!engine.storage().mounted()) {
    fail("filesystem unavailable");
    return false;
  }

  state_ = StoreState::kInstalling;
  const StoreIndex::Entry& entry = index_.at(selected_);

  Serial.print(F("[store] installing "));
  Serial.println(entry.id);

  if (collidesWithBuiltIn(entry.id)) {
    fail("id collides with built-in");
    return false;
  }

  char dir[32];
  char tmp_script[48];
  char script[48];
  char bak_script[48];
  char tmp_meta[48];
  char meta[48];
  char bak_meta[48];
  snprintf(dir, sizeof(dir), "/games/%s", entry.id);
  snprintf(tmp_script, sizeof(tmp_script), "%s/game.tmp", dir);
  snprintf(script, sizeof(script), "%s/game.be", dir);
  snprintf(bak_script, sizeof(bak_script), "%s/game.bak", dir);
  snprintf(tmp_meta, sizeof(tmp_meta), "%s/meta.tmp", dir);
  snprintf(meta, sizeof(meta), "%s/meta.json", dir);
  snprintf(bak_meta, sizeof(bak_meta), "%s/meta.bak", dir);

  if (!LittleFS.exists("/games") && !LittleFS.mkdir("/games")) {
    fail("cannot create games folder");
    return false;
  }

  if (!LittleFS.exists(dir) && !LittleFS.mkdir(dir)) {
    fail("cannot create folder");
    return false;
  }

  LittleFS.remove(tmp_script);
  LittleFS.remove(tmp_meta);

  const char* error = "";
  if (!downloadScript(entry, tmp_script, &error)) {
    fail(error);
    return false;
  }

  if (!writeMeta(entry, tmp_meta)) {
    LittleFS.remove(tmp_script);
    fail("cannot write meta");
    return false;
  }

  LittleFS.remove(bak_script);
  LittleFS.remove(bak_meta);

  bool ok = true;
  if (LittleFS.exists(script)) ok = LittleFS.rename(script, bak_script);
  if (ok && LittleFS.exists(meta)) ok = LittleFS.rename(meta, bak_meta);
  ok = ok && LittleFS.rename(tmp_script, script);
  ok = ok && LittleFS.rename(tmp_meta, meta);

  if (!ok) {
    LittleFS.remove(script);
    LittleFS.remove(meta);
    if (LittleFS.exists(bak_script)) LittleFS.rename(bak_script, script);
    if (LittleFS.exists(bak_meta)) LittleFS.rename(bak_meta, meta);
    LittleFS.remove(tmp_script);
    LittleFS.remove(tmp_meta);
    fail("install rename failed");
    return false;
  }

  LittleFS.remove(bak_script);
  LittleFS.remove(bak_meta);

  CartridgeStore store;
  store.scan();
  gameList().build(store);
  if (!installedGameVisible(entry.id)) {
    fail("install not visible");
    return false;
  }

  state_ = StoreState::kSuccess;
  settled_at_ms_ = millis();
  Serial.print(F("[store] installed "));
  Serial.println(entry.id);
  return true;
}

void StoreScene::update(Engine& engine, float dt) {
  phase_ += dt;
  if (phase_ > 3600.0f) phase_ -= 3600.0f;

  net_.tick();

  if (state_ == StoreState::kConnecting) {
    if (net_.state() == NetState::kConnected) {
      Display& display = engine.display();
      display.clear();
      drawBusy(engine, kFetchColor);
      display.present();
      fetchIndex();
    } else if (net_.state() == NetState::kFailed) {
      fail("connect failed");
    }
    return;
  }

  if (state_ == StoreState::kReady) {
    if (index_.count() == 0) return;

    const int8_t step = engine.input().navDelta();
    if (step != 0) {
      const int16_t next = static_cast<int16_t>(selected_) + step;
      if (next >= 0 && next < static_cast<int16_t>(index_.count())) {
        selected_ = static_cast<uint8_t>(next);
      }
    }

    if (engine.input().pressed(Button::kA) ||
        engine.input().pressed(Input::kNavButton)) {
      Display& display = engine.display();
      display.clear();
      display.span(0.0f, 1.0f, kInstallColor, 0.35f);
      display.present();
      installSelected(engine);
    }
  }
}

void StoreScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  switch (state_) {
    case StoreState::kConnecting:
      drawBusy(engine, kConnectColor);
      break;
    case StoreState::kFetching:
      drawBusy(engine, kFetchColor);
      break;
    case StoreState::kInstalling:
      display.span(0.0f, 1.0f, kInstallColor, 0.35f);
      break;
    case StoreState::kReady:
      drawReady(engine);
      break;
    case StoreState::kSuccess: {
      const uint32_t since = millis() - settled_at_ms_;
      const float t = since >= kResultFlashMs
                          ? 1.0f
                          : static_cast<float>(since) / kResultFlashMs;
      display.span(0.5f - t * 0.5f, 0.5f + t * 0.5f, kOkColor, 0.9f);
      break;
    }
    case StoreState::kFailed:
      display.span(0.0f, 1.0f, kFailColor,
                   0.2f + 0.4f * wrappedSin(phase_ * 8.0f));
      break;
  }
}

void StoreScene::drawBusy(Engine& engine, const Color& color) {
  Display& display = engine.display();
  const float head = fmodf(phase_ * 1.6f, 1.0f);
  display.point(head, color);
  display.point(head - display.pixelWidth(), color, 0.5f);
  display.point(head - display.pixelWidth() * 2.0f, color, 0.2f);
}

void StoreScene::drawReady(Engine& engine) {
  Display& display = engine.display();
  if (index_.count() == 0) {
    display.span(0.0f, 1.0f, kOkColor, 0.12f);
    return;
  }

  const float step = 1.0f / index_.count();
  for (uint8_t i = 0; i < index_.count(); i++) {
    const float center = (i + 0.5f) * step;
    const float half = fmaxf(display.pixelWidth(), step * 0.35f);
    const bool selected = i == selected_;
    const float intensity =
        selected ? 0.55f + 0.35f * wrappedSin(phase_ * 5.0f) : 0.15f;
    Color color(255, 140, 0);
    // Reuse CartridgeStore's stricter colour validation later during install;
    // the index parser already guarantees this is six hex digits, so this small
    // conversion cannot fail.
    uint32_t packed = strtoul(index_.at(i).color, nullptr, 16);
    color = Color(static_cast<uint8_t>((packed >> 16) & 0xFF),
                  static_cast<uint8_t>((packed >> 8) & 0xFF),
                  static_cast<uint8_t>(packed & 0xFF));
    display.span(center - half, center + half, color, intensity);
  }
}

}  // namespace beamboy
