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
// WiFiManager owns the main loop (it blocks in autoConnect /
// startConfigPortal), which is incompatible with rule 2 -- the tube must keep
// animating while the portal is open, since that animation is the only feedback
// the user has.

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <atomic>

#include "net_policy.h"

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
  bool scanning() const { return scan_pending_ || scan_requested_; }

  // Progress of the current operation, 0..1, for drawing on the tube. Only
  // meaningful while connecting.
  float progress() const;

  // Why the failure happened, so the UI can say something more useful than
  // "it didn't work" and so provisioning can decide whether to keep the
  // credentials. Only meaningful in kFailed.
  // Why the last attempt failed. Defined in net_policy.h so the decision rules
  // can be host-tested without the WiFi stack; re-exported here so callers can
  // keep saying Network::FailReason.
  using FailReason = beamboy::FailReason;
  FailReason failReason() const { return fail_reason_; }

  // True when the stored credentials have never yet produced a connection.
  // Provisioning keeps them only long enough to retry; see tick()'s kFailed
  // handling for why they are dropped on a definite rejection but kept on a
  // transient one.
  bool credentialsUnproven() const { return unproven_; }

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
  //
  // Kept modest deliberately. The driver holds a ~72-byte bss_info per result
  // for the whole portal session, so this is heap that stays occupied while the
  // soft-AP needs it. A list longer than this is also not usefully browsable on
  // a phone, and the "Other (type below)" field covers anything cut off.
  static constexpr int8_t kMaxScanResults = 15;

  bool loadCredentials();
  bool saveCredentials(const char* ssid, const char* password);

  // Classifies a failed connection attempt from the driver's status.
  FailReason classifyFailure(bool deadline_reached) const;
  bool authRejectedThisAttempt() const;
  void registerEventHandlers();
  void onDisconnected(int reason);

  // Enters kFailed, recording why, and drops credentials that have been proven
  // wrong rather than merely unlucky.
  void failWith(FailReason reason);

  void setState(NetState next);
  void startServer();
  void stopServer();

  // HTTP handlers.
  void handleRoot();
  void handleRescan();
  void handleSave();
  void handleNotFound();

  // Requests a scan. Does NOT touch the WiFi driver -- it only raises
  // scan_requested_, which tick() acts on. See startRequestedScan() for why the
  // call has to be deferred rather than made here.
  void requestScan();

  // Issues the driver call for a pending request, if there is one. Only ever
  // called from tick(), at a point where suspending is safe.
  void startRequestedScan();

  // Collects a finished async scan into scan_count_. Cheap; called every tick.
  void pollScan();
  // Logs free heap and largest contiguous block. The portal path is where the
  // device runs closest to out-of-memory, and an allocation failure there
  // manifests as a fault inside whichever core function was allocating rather
  // than as an error return.
  static void logHeap(const __FlashStringHelper* label);
  // HTML-escapes an SSID before it is placed into the setup page.
  static void appendEscaped(String& out, const String& raw);

  char ssid_[kSsidLength] = {0};
  char password_[kPasswordLength] = {0};

  // Set while stored credentials have never produced a connection -- i.e. they
  // came from the portal and the first attempt has not succeeded yet. Cleared
  // the moment a connection is established, and never persisted: credentials
  // that survive a reboot have either been proven or were kept deliberately.
  bool unproven_ = false;

  // Verdict capture, written by the disconnect event handler.
  //
  // That handler runs on the WiFi event task while tick() runs on the loop
  // task, so these must be atomic -- plain bools would be a data race, and
  // there would be no guarantee tick() ever observed the writes.
  //
  // They hold a *generation* rather than a flag. Events are queued, so one
  // produced while tearing down an attempt can be delivered after the next has
  // started; recording which attempt was current when the event arrived lets
  // tick() ignore anything that is not about the attempt it is waiting on.
  // kNoAttempt means no attempt is in flight, and the handler ignores events
  // entirely.
  static constexpr uint32_t kNoAttempt = 0;
  std::atomic<uint32_t> attempt_gen_{kNoAttempt};
  std::atomic<uint32_t> auth_rejected_gen_{kNoAttempt};
  std::atomic<uint32_t> no_ap_found_gen_{kNoAttempt};

  // Source of the next generation number. Only ever incremented, on the loop
  // task, so it needs no synchronisation of its own.
  uint32_t next_attempt_gen_ = 1;

  bool wifi_events_registered_ = false;

  FailReason fail_reason_ = FailReason::kNone;

  // begin() is called on every scene entry, not once at boot. This
  // distinguishes the first call so that per-session state is not reset each
  // time the user walks into the Network scene.
  bool began_ = false;

  NetState state_ = NetState::kOff;
  uint32_t state_started_ms_ = 0;

  DNSServer dns_;
  WebServer server_{kHttpPort};
  bool server_running_ = false;
  // Routes are registered once for the lifetime of the object; see
  // startServer().
  bool handlers_registered_ = false;

  // Number of networks in the driver's completed scan buffer, or 0. Results
  // stay owned by the WiFi driver until scanDelete(), so only the count is kept
  // here.
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

  // A scan has been asked for but not yet handed to the driver. Kept separate
  // from scan_pending_ because the driver call is deferred to tick().
  bool scan_requested_ = false;
};

}  // namespace beamboy
