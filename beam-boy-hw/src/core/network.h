#pragma once

// Beam Boy — networking.
//
// Design rules, in priority order:
//
// 1. **The radio is off until the user asks for it.** Not "off until needed" --
//    off. A Beam Boy with no credentials never scans, never associates, never
//    delays boot. Offline is the default state, not a fallback.
//
// 2. **Nothing here may block the frame loop.** The obvious implementation
//    (WiFi.begin() then a while-loop on status) stalls for seconds and freezes
//    the tube. Instead this is a state machine: begin an operation, return
//    immediately, and poll it from tick() once per frame. Every state carries
//    its own timeout so no state can be entered and never left.
//
// 3. **Credentials live in their own file**, not in the game save. Resetting
//    highscores must not log you out, and a save-format version bump must not
//    drop the network config.
//
// The captive portal is deliberately hand-rolled rather than using WiFiManager.
// WiFiManager owns the main loop (it blocks in autoConnect / startConfigPortal),
// which is incompatible with rule 2 -- the tube must keep animating while the
// portal is open, since that animation is the only feedback the user has.

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#else
#include <WebServer.h>
#include <WiFi.h>
#endif

#include <DNSServer.h>

namespace beamboy {

// What the network is doing right now. The NetworkScene maps these to colours
// on the tube; nothing else should need to care.
enum class NetState : uint8_t {
  kOff,           // Radio down. The default, and the state we return to.
  kConnecting,    // Trying stored credentials.
  kConnected,     // Associated and has an IP.
  kFailed,        // Tried and gave up. Distinct from kOff: the user asked.
  kPortalActive,  // SoftAP up, waiting for someone to submit the form.
  kPortalSaved,   // Credentials received; about to try them.
};

class Network {
 public:
  // Loads stored credentials but does NOT touch the radio. Safe to call at
  // boot; deliberately cheap and non-blocking.
  void begin();

  // Must be called every frame while the network is doing anything. Drives the
  // connection state machine and services the portal's DNS and HTTP requests.
  // Returns immediately when state is kOff.
  void tick();

  NetState state() const { return state_; }

  // True if credentials are stored -- i.e. the user has provisioned this device
  // at some point. Used to decide whether "Network" offers connect or setup.
  bool hasCredentials() const { return ssid_[0] != '\0'; }

  const char* ssid() const { return ssid_; }

  // True while a network scan is running. The portal states look identical
  // whether or not one is in flight, so the scene uses this to show that
  // something is happening.
  bool scanning() const { return scan_pending_; }

  // Progress of the current operation, 0..1, for drawing on the tube. Only
  // meaningful while connecting.
  float progress() const;

  // --- Actions ---------------------------------------------------------------

  // Brings the radio up and tries the stored credentials. No-op without them.
  void connect();

  // Opens the configuration portal: a SoftAP named "BeamBoy-Setup" plus a DNS
  // server that resolves every query to us, so any URL opens the form.
  void startPortal();

  // Drops the radio entirely and returns to kOff. Also called on scene exit so
  // the radio is never left on by accident.
  void disconnect();

  // Erases stored credentials from flash.
  void forget();

  // Local address, valid in kConnected and kPortalActive. For serial logging --
  // the device has no way to display an IP.
  String address() const;

 private:
  static constexpr uint32_t kConnectTimeoutMs = 20000;
  static constexpr uint32_t kPortalTimeoutMs = 300000;  // 5 minutes
  static constexpr uint8_t kSsidLength = 33;            // 32 + NUL
  static constexpr uint8_t kPasswordLength = 64;        // 63 + NUL
  static constexpr uint16_t kDnsPort = 53;
  static constexpr uint16_t kHttpPort = 80;
  // Caps the rendered list. Also bounds scan_count_, which is int8_t.
  static constexpr int8_t kMaxScanResults = 30;

  bool loadCredentials();
  bool saveCredentials(const char* ssid, const char* password);

  void setState(NetState next);
  void startServer();
  void stopServer();

  // HTTP handlers.
  void handleRoot();
  void handleRescan();
  void handleSave();
  void handleNotFound();

  // Starts an asynchronous scan. Never blocks; the result is collected by
  // pollScan(). Requires the station half to be up (AP_STA is enough).
  void runScan();

  // Collects a finished async scan into scan_count_. Cheap; called every tick.
  void pollScan();
  // HTML-escapes an SSID before it is placed into the setup page.
  static void appendEscaped(String& out, const String& raw);

  char ssid_[kSsidLength] = {0};
  char password_[kPasswordLength] = {0};

  NetState state_ = NetState::kOff;
  uint32_t state_started_ms_ = 0;

  DNSServer dns_;
#if defined(ARDUINO_ARCH_ESP8266)
  ESP8266WebServer server_{kHttpPort};
#else
  WebServer server_{kHttpPort};
#endif
  bool server_running_ = false;
  // Routes are registered once for the lifetime of the object; see startServer().
  bool handlers_registered_ = false;

  // Number of networks in the driver's completed scan buffer, or 0. Results stay
  // owned by the WiFi driver until scanDelete(), so only the count is kept here.
  //
  // Starting a new scan frees the previous buffer, so while scan_pending_ is
  // true this count can be stale and describe a buffer that no longer exists.
  // Indexing it is nonetheless safe -- both cores bounds-check every accessor
  // and return empty values -- but callers should prefer the pending flag when
  // deciding what to show, or they will render an empty list as if the scan had
  // found nothing.
  int8_t scan_count_ = 0;

  // A scan has been started and not yet collected by pollScan().
  bool scan_pending_ = false;
};

}  // namespace beamboy
