#include "network.h"

#include <LittleFS.h>

namespace beamboy {
namespace {

constexpr char kCredentialsPath[] = "/net.cfg";
constexpr char kApName[] = "BeamBoy-Setup";

// The setup page. Deliberately one self-contained string with no external CSS,
// fonts or scripts: the portal has no internet route, so any external reference
// would hang until it times out. Kept in PROGMEM to keep it out of RAM.
const char kSetupPageHead[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>Beam Boy</title><style>
body{background:#111;color:#eee;font-family:system-ui,sans-serif;margin:0;padding:2em 1.5em}
h1{font-size:1.3em;letter-spacing:.2em;color:#0f8;margin:0 0 .2em}
p{color:#888;font-size:.9em;margin:0 0 2em}
label{display:block;margin:1.2em 0 .3em;font-size:.85em;color:#aaa}
input,select{width:100%;box-sizing:border-box;padding:.7em;font-size:1em;background:#222;
color:#eee;border:1px solid #444;border-radius:6px}
button{width:100%;margin-top:2em;padding:.9em;font-size:1em;font-weight:600;
background:#0f8;color:#111;border:0;border-radius:6px}
.hint{font-size:.8em;color:#777;margin:1.2em 0 0}
.hint a{color:#0f8}
</style></head><body>
<h1>BEAM BOY</h1><p>Connect your console to WiFi.</p>
<form action="/save" method="post">
<label for=s>Network name</label>)HTML";

// Shown when the scan found nothing: the text field is the only way in. A scan
// can legitimately come back empty (hidden SSIDs, 5 GHz-only networks -- neither
// chip can see those), so this is a normal path, not an error.
const char kSetupPageManual[] PROGMEM =
    R"HTML(<input id=s name=s maxlength=32 autofocus>)HTML";

// Closes the form but NOT the document -- handleRoot() appends the rescan hint
// after this, outside the form, and closes body/html itself.
const char kSetupPageTail[] PROGMEM = R"HTML(
<label for=p>Password</label><input id=p name=p type=password maxlength=63>
<button type=submit>Save &amp; connect</button>
</form>)HTML";

// Sent immediately in reply to /rescan, before the scan starts. The refresh
// interval is generous: an ESP32 scan at 500 ms per channel can take several
// seconds, and reloading too early just shows the old list.
const char kScanningPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta name=viewport content="width=device-width,initial-scale=1">
<meta http-equiv=refresh content="8;url=/">
<title>Beam Boy</title><style>
body{background:#111;color:#eee;font-family:system-ui,sans-serif;margin:0;padding:3em 1.5em}
h1{font-size:1.2em;color:#0f8}p{color:#888;font-size:.9em}a{color:#0f8}
</style></head><body>
<h1>Scanning&hellip;</h1><p>This takes a few seconds. The list will reload on its
own &mdash; or <a href="/">tap here</a> if it does not.</p>
</body></html>)HTML";

const char kSavedPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>Beam Boy</title><style>
body{background:#111;color:#eee;font-family:system-ui,sans-serif;margin:0;padding:3em 1.5em}
h1{font-size:1.2em;color:#0f8}p{color:#888}
</style></head><body>
<h1>Saved</h1><p>Watch the tube: a blue sweep means connecting, green means
connected, red means it failed. You can close this page.</p>
</body></html>)HTML";

}  // namespace

void Network::begin() {
  loadCredentials();

  // begin() runs on every *scene entry*, not once at boot, so this must not
  // clobber state that belongs to the session. Whether credentials are unproven
  // survives leaving and re-entering the Network scene: otherwise a user who
  // typed a bad password, backed out to the launcher and came back would find
  // them silently promoted to "proven", and the discard rule -- the whole point
  // of tracking this -- would never fire again until a reboot.
  //
  // A power cycle is the one event that does reset it. After a restart there is
  // no way to tell a never-tested network from a working one the user is simply
  // away from, and guessing wrong would erase a good configuration, so anything
  // that survived a reboot is treated as proven.
  if (!began_) {
    began_ = true;
    unproven_ = false;
    fail_reason_ = FailReason::kNone;
  }

  // Make sure a previous session (or the bootloader's persisted config) has not
  // left the radio on. Arduino cores persist WiFi settings in flash and will
  // auto-reconnect at boot unless told not to -- which would violate the
  // radio-off-by-default rule before our code ever runs.
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.setAutoReconnect(false);

  state_ = NetState::kOff;
}

bool Network::loadCredentials() {
  ssid_[0] = '\0';
  password_[0] = '\0';

  if (!LittleFS.exists(kCredentialsPath)) return false;

  File f = LittleFS.open(kCredentialsPath, "r");
  if (!f) return false;

  // Stored as two newline-terminated lines. readStringUntil is bounded by the
  // file size, and both fields are length-checked on write.
  const String ssid = f.readStringUntil('\n');
  const String password = f.readStringUntil('\n');
  f.close();

  if (ssid.length() == 0 || ssid.length() >= kSsidLength) return false;
  if (password.length() >= kPasswordLength) return false;

  strncpy(ssid_, ssid.c_str(), kSsidLength - 1);
  ssid_[kSsidLength - 1] = '\0';
  strncpy(password_, password.c_str(), kPasswordLength - 1);
  password_[kPasswordLength - 1] = '\0';
  return true;
}

bool Network::saveCredentials(const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0') return false;
  if (strlen(ssid) >= kSsidLength) return false;
  if (password != nullptr && strlen(password) >= kPasswordLength) return false;

  File f = LittleFS.open(kCredentialsPath, "w");
  if (!f) return false;

  bool ok = true;
  ok = ok && f.print(ssid) == strlen(ssid);
  ok = ok && f.print('\n') == 1;
  if (password != nullptr && password[0] != '\0') {
    ok = ok && f.print(password) == strlen(password);
  }
  ok = ok && f.print('\n') == 1;
  f.close();

  // A partial write leaves a file that parses but connects to nothing, which
  // looks like a hardware fault to the user. Better to have no file at all.
  if (!ok) {
    LittleFS.remove(kCredentialsPath);
    return false;
  }

  strncpy(ssid_, ssid, kSsidLength - 1);
  ssid_[kSsidLength - 1] = '\0';
  if (password != nullptr) {
    strncpy(password_, password, kPasswordLength - 1);
    password_[kPasswordLength - 1] = '\0';
  } else {
    password_[0] = '\0';
  }
  return true;
}

void Network::forget() {
  LittleFS.remove(kCredentialsPath);
  ssid_[0] = '\0';
  password_[0] = '\0';
  unproven_ = false;
  fail_reason_ = FailReason::kNone;

  // If the radio is currently using the credentials being erased, take it down
  // too. Otherwise "forget" leaves the device still associated to the network it
  // claims to have forgotten -- and still drawing power for it -- until the next
  // reboot, which is not what the user asked for.
  if (state_ != NetState::kOff) disconnect();
}

bool Network::authRejectedThisAttempt() const {
  const uint32_t gen = attempt_gen_.load();
  return gen != kNoAttempt && auth_rejected_gen_.load() == gen;
}

Network::FailReason Network::classifyFailure(bool deadline_reached) const {
  // Authentication verdicts come from the *disconnect reason*, captured by the
  // event handler and matched to this attempt's generation. Only that is
  // authoritative.
  //
  // wl_status_t deliberately is not used to detect a bad password. It cannot:
  //   - WL_CONNECT_FAILED is not an authentication verdict -- it also covers
  //     WIFI_REASON_ASSOC_FAIL (an AP at capacity, for instance), and reading it
  //     as "wrong password" would erase perfectly good credentials.
  //   - It is a cached value updated by events, so straight after WiFi.begin()
  //     it can still describe the *previous* attempt.
  if (authRejectedThisAttempt()) return FailReason::kBadPassword;

  // Not-found is safe to read from status: it says the AP was never heard, and
  // it never implies anything about the key.
  if (WiFi.status() == WL_NO_SSID_AVAIL) return FailReason::kNotFound;

  const uint32_t gen = attempt_gen_.load();
  if (deadline_reached && gen != kNoAttempt &&
      no_ap_found_gen_.load() == gen) {
    return FailReason::kNotFound;
  }

  // Anything else is inconclusive and stays that way. An AP that is weak,
  // intermittent, full, or slow through DHCP all land here, and none of them
  // are grounds for erasing credentials.
  return FailReason::kTimeout;
}

void Network::failWith(FailReason reason) {
  attempt_gen_.store(kNoAttempt);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  fail_reason_ = reason;

  // Discard credentials that have been *proven* wrong, but only those, and only
  // while they are still unproven. The full reasoning for that two-part rule --
  // and why the user's simpler "never store a network we couldn't reach" would
  // misfire on transient failures -- lives with the rule itself, in
  // net_policy.h.
  if (shouldDiscardCredentials(reason, unproven_)) {
    // Only claim the credentials are gone if they are actually gone. If the
    // remove fails and RAM is cleared anyway, the menu drops to Setup while the
    // file quietly survives -- and the next scene entry calls loadCredentials()
    // and brings the same unusable network straight back, now looking "proven".
    // Keeping them is the lesser evil: Forget still works, and the user is not
    // told something false. Serial is not visible on the handheld, so the state
    // has to stay honest on its own.
    if (LittleFS.remove(kCredentialsPath)) {
      Serial.println(F("[net] password rejected -- discarding credentials"));
      ssid_[0] = '\0';
      password_[0] = '\0';
      unproven_ = false;
    } else {
      Serial.println(F("[net] WARNING: could not erase /net.cfg -- keeping"));
    }
  }

  setState(NetState::kFailed);
}

void Network::setState(NetState next) {
  state_ = next;
  state_started_ms_ = millis();
}

void Network::connect() {
  if (!hasCredentials()) return;

  stopServer();
  // Reached from the portal too, where a scan is still holding memory.
  WiFi.scanDelete();
  scan_count_ = 0;
  scan_pending_ = false;
  scan_requested_ = false;

  // Open a new generation for this attempt. Verdict events are stamped with it,
  // so anything still queued from a previous attempt can no longer be mistaken
  // for this one's result -- the case that would otherwise erase a password the
  // user had just corrected.
  attempt_gen_.store(next_attempt_gen_++);
  if (next_attempt_gen_ == kNoAttempt) next_attempt_gen_ = 1;  // skip sentinel

  WiFi.mode(WIFI_STA);
  registerEventHandlers();
  WiFi.begin(ssid_, password_);
  fail_reason_ = FailReason::kNone;
  setState(NetState::kConnecting);
}

void Network::registerEventHandlers() {
  if (wifi_events_registered_) return;
  wifi_events_registered_ = true;

  // Why events rather than WiFi.status(): the status word collapses every kind
  // of failure into a handful of values, and it lags behind the driver. The
  // disconnect *reason* is the only place the radio says why it gave up, and it
  // is the difference between "your password is wrong" and "that access point
  // is full".

  WiFi.onEvent(
      [this](arduino_event_id_t, arduino_event_info_t info) {
        onDisconnected(info.wifi_sta_disconnected.reason);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void Network::onDisconnected(int reason) {
  // Runs on the WiFi event task, not the loop task, so everything it touches
  // is atomic and it stays short.
  //
  // Correlation is by *generation*, not by an in-progress flag. Events are
  // queued to another task, so a failure generated while tearing down one
  // attempt can be delivered after the next has already begun -- and
  // attributing that to freshly corrected credentials is precisely how a
  // password the user just got right would get erased. Stamping the event with
  // the generation that was current when it arrived lets tick() ignore anything
  // that does not belong to the attempt it is waiting on.
  const uint32_t gen = attempt_gen_.load();
  if (gen == kNoAttempt) return;

  // A wrong WPA key nearly always fails in the 4-way handshake: the AP accepts
  // association, then goes quiet once it sees the wrong MIC. Reaching the
  // handshake proves association succeeded, so the key is the only thing left
  // that can be wrong.
  //
  // Excluded deliberately: ASSOC_FAIL and neighbours (capacity and
  // compatibility, not credentials); AUTH_EXPIRE (a lapsed prior
  // authentication); MIC_FAILURE and the generic HANDSHAKE_TIMEOUT, both
  // reachable through interference, packet loss and key rotation. A false
  // positive here erases a good password, so the list stays narrow.
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      auth_rejected_gen_.store(gen);
      break;
    case WIFI_REASON_NO_AP_FOUND:
      no_ap_found_gen_.store(gen);
      break;
    default:
      break;
  }
}


void Network::startPortal() {
  // AP+STA rather than plain AP. The station half never connects to anything --
  // begin() disables auto-connect and no WiFi.begin() is issued here -- but its
  // presence is what lets a scan run while the AP stays up. Switching to
  // WIFI_STA to scan would drop the phone entirely.
  //
  // Note this does not make scanning free: the chip has one radio, so it still
  // leaves our channel to sweep, and traffic is dropped while it does. It keeps
  // the phone associated rather than deauthenticated, which is the difference
  // between a page that stalls briefly and one that fails outright.
  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP(kApName);

  // Resolve every hostname to us. This is what makes the phone's captive-portal
  // check fail in the specific way that pops the login sheet: the OS requests a
  // known URL, DNS points it here, and we answer with something other than the
  // expected 204/success body.
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  // DNSServer::start() takes its port by reference, which odr-uses the constant
  // and would demand an out-of-line definition. Copying to a local sidesteps it.
  uint16_t dns_port = kDnsPort;
  dns_.start(dns_port, "*", WiFi.softAPIP());

  startServer();
  setState(NetState::kPortalActive);

  // Only now, with the AP, DNS and HTTP server all up and the state settled, is
  // it safe to ask for a scan -- tick() will issue it on a later pass. Doing it
  // any earlier ran the loop against a half-built portal; see
  // startRequestedScan().
  requestScan();
}

void Network::disconnect() {
  // Ends any attempt in progress. Everything after this point causes disconnect
  // events of our own making, and none of them say anything about the user's
  // credentials -- so close the generation before tearing the radio down.
  attempt_gen_.store(kNoAttempt);

  stopServer();
  dns_.stop();

  // Scan results live in memory owned by the WiFi driver until explicitly
  // deleted. Leaving them allocated would leak a few hundred bytes per portal
  // session in an object that is never destroyed -- the same shape as the
  // handler leak found in review, and it would surface the same way, as a TLS
  // handshake failing later for want of contiguous heap.
  //
  // Note scanDelete() frees the completed buffer but does not cancel a scan
  // still in flight; only the kPortalSaved path can afford to wait for one
  // (see tick()). This path is user-initiated cancellation and has to be
  // immediate, so it accepts that a pending scan may complete into a radio that
  // is being torn down. That is survivable here because WIFI_OFF follows
  // immediately and the state machine is left in kOff, whereas the kPortalSaved
  // path went straight on to bring a station up on top of it.
  WiFi.scanDelete();
  scan_count_ = 0;
  scan_pending_ = false;
  scan_requested_ = false;

  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  state_ = NetState::kOff;
}

void Network::startServer() {
  if (server_running_) return;

  // Register the routes exactly once. Both cores' WebServer heap-allocate a
  // handler object per on()/onNotFound() call and only free them in the
  // destructor -- stop() just closes the socket. Since this Network lives in a
  // file-static scene that is never destroyed, re-registering on every portal
  // open would permanently leak a handler per route each time and append
  // duplicate routes, steadily eroding the contiguous block the TLS handshake
  // needs.
  if (!handlers_registered_) {
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/rescan", HTTP_GET, [this]() { handleRescan(); });
    server_.on("/save", HTTP_POST, [this]() { handleSave(); });
    server_.onNotFound([this]() { handleNotFound(); });
    handlers_registered_ = true;
  }

  server_.begin();
  server_running_ = true;
}

void Network::stopServer() {
  if (!server_running_) return;
  server_.stop();
  server_running_ = false;
}

void Network::requestScan() {
  scan_requested_ = true;
}

void Network::startRequestedScan() {
  if (!scan_requested_ || scan_pending_) return;
  scan_requested_ = false;

  // Asynchronous, and this is not optional. A synchronous scan blocks for up to
  // ~6.5 s (13 channels x 500 ms dwell).
  //
  // "Asynchronous" is weaker than it sounds, which is why this call is reached
  // only from tick() and never directly from a caller. Two properties of the
  // core's scanNetworks() make it unsafe to treat as an ordinary non-blocking
  // function:
  //
  //  - Even on the async path it ends with esp_yield(), which suspends the loop
  //    continuation and returns to the SDK, "to give the OS time to trigger the
  //    scan". The continuation is later resumed in place rather than restarted,
  //    so this is not re-entrancy -- nothing runs on top of this frame. The
  //    hazard is the gap itself: arbitrary SDK and driver code runs before the
  //    next statement here executes, and whatever the caller had not done yet
  //    stays undone across it. Calling this partway through startPortal() left
  //    the device sitting in the SDK with no soft-AP, no DNS server and the
  //    state still kConnecting, which crashed as soon as a phone associated.
  //
  //  - It calls enableSTA() and wifi_station_disconnect() internally, so the
  //    radio is reconfigured underneath the caller during exactly that gap.
  //
  // Deferring to the end of tick() makes both harmless: the gap then falls at a
  // point where the portal is fully constructed and no HTTP handler is part-way
  // through, so there is no half-finished work for the SDK to trip over. It also
  // keeps the scan out of handleRescan(), where suspending with
  // server_.handleClient() further down the stack is its own trap.
  //
  // Requires the station half to be up (AP_STA is enough).
  //
  // show_hidden = true: a hidden AP reports an empty SSID, which is filtered out
  // when rendering, but asking costs nothing.
  //
  // The per-channel dwell is exposed as a 4th argument. The default is 300 ms;
  // 500 gives slow-beaconing APs another chance to be heard. Async, so the cost
  // is paid in wall-clock time rather than in a stalled loop.
  WiFi.scanNetworks(true, true, false, 500);
  scan_pending_ = true;
}

void Network::pollScan() {
  if (!scan_pending_) return;

  // Negative means still running (-1) or failed (-2). Only a non-negative result
  // is a completed scan whose buffer is safe to index.
  const int16_t result = WiFi.scanComplete();
  if (result < 0) {
    if (result == WIFI_SCAN_FAILED) {
      scan_pending_ = false;
      scan_count_ = 0;
      Serial.println(F("[net] scan failed"));
    }
    return;
  }

  scan_pending_ = false;
  scan_count_ =
      result > kMaxScanResults ? kMaxScanResults : static_cast<int8_t>(result);
  Serial.print(F("[net] scan found "));
  Serial.print(scan_count_);
  logHeap(F(" heap"));
}

// The portal is the tightest the heap ever gets: the soft-AP allocates a pbuf
// per received frame, an HTTP request is in flight, and a scan result buffer is
// live. When an allocation fails in that state the SDK does not report it -- it
// faults, and the reported PC lands in whichever allocating function happened to
// be running, which is why successive crashes pointed at unrelated core
// functions. Free heap alone is not enough to see this coming: what matters is
// the largest contiguous block, since fragmentation can starve a single large
// allocation while the total still looks healthy.
void Network::logHeap(const __FlashStringHelper* label) {
  Serial.print(label);
  Serial.print(F(" free="));
  Serial.print(ESP.getFreeHeap());
  Serial.print(F(" max_block="));
  Serial.println(ESP.getMaxAllocHeap());
}

void Network::handleRescan() {
  logHeap(F("[net] rescan"));
  // Respond first, then only *request* the scan. The reply is a short page that
  // waits and reloads itself, which gives the user something to look at instead
  // of a silently-stalled form. The scan itself must not start while this
  // handler is on the stack (see startRequestedScan), so tick() issues it once
  // this handler has returned.
  server_.send_P(200, "text/html", kScanningPage);
  requestScan();
}

void Network::handleRoot() {
  logHeap(F("[net] root"));

  // Streamed in chunks rather than assembled into one String. The portal is the
  // tightest the heap ever gets -- the soft-AP allocates a pbuf per received
  // frame, and it cannot wait or report failure: when an allocation fails in
  // that state the core faults, and the reported PC lands in whichever function
  // happened to be allocating. That is why successive crashes pointed at
  // unrelated core internals (hostap_input, then
  // run_scheduled_recurrent_functions) with a null excvaddr. A wandering fault
  // address is the signature of memory exhaustion, not of a bug at either site.
  //
  // A 2 KB page String also has to *grow*: each reallocation briefly holds the
  // old and new buffers at once and leaves a hole behind, so the largest
  // contiguous block shrinks faster than the free total suggests. Chunked
  // transfer keeps only one small buffer alive at a time, so peak usage no
  // longer scales with the number of networks found.
  //
  // setContentLength(CONTENT_LENGTH_UNKNOWN) makes the server emit
  // Transfer-Encoding: chunked for HTTP/1.1 clients, which is what keeps the
  // connection open between chunks -- the reason the page was built in one
  // piece originally.
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/html", "");
  server_.sendContent_P(kSetupPageHead);

  // Decide between the dropdown and the plain text field with a counting
  // pre-pass. It has to be known before the <select> is emitted, and once
  // streaming has begun nothing can be taken back.
  //
  // A rescan frees the previous result buffer the moment it starts, so
  // scan_count_ can still be non-zero while every entry reads back empty --
  // hence counting rendered entries rather than trusting the count, which
  // avoids emitting a <select> containing nothing but "Other".
  uint8_t visible = 0;
  if (scan_count_ > 0 && !scan_pending_) {
    for (int8_t i = 0; i < scan_count_; ++i) {
      if (WiFi.SSID(i).length() > 0) ++visible;
    }
  }

  if (visible > 0) {
    server_.sendContent(F("<select id=s name=s>"));
    // One <option> per chunk. Bounded by the length of a single SSID rather
    // than by the size of the whole list.
    String option;
    option.reserve(96);
    for (int8_t i = 0; i < scan_count_; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;  // Hidden network: nothing to show.
      option = F("<option value=\"");
      appendEscaped(option, ssid);
      option += F("\">");
      appendEscaped(option, ssid);
      // Signal strength, so it is obvious which of two similar names is yours.
      option += WiFi.RSSI(i) >= -67 ? F(" &middot;&middot;&middot;") : F(" &middot;");
      option += F("</option>");
      server_.sendContent(option);
    }
    // A network can be hidden, or 5 GHz-only and thus invisible to this chip, so
    // there must always be a way to type a name the scan never found.
    server_.sendContent(F("<option value=\"\">Other (type below)</option></select>"
                          "<label for=m>Or type the name</label>"
                          "<input id=m name=m maxlength=32>"));
  } else {
    server_.sendContent_P(kSetupPageManual);
  }

  server_.sendContent_P(kSetupPageTail);

  // A single scan genuinely misses access points, so an incomplete list is
  // expected rather than exceptional -- say so where it will be read after the
  // user fails to find their network, and make retrying one tap.
  server_.sendContent(F("<p class=hint>"));
  if (scan_pending_) {
    // Distinguishes "still looking" from "looked and found nothing". Without
    // this, a form loaded while the first scan is still running looks like a
    // scan that failed.
    server_.sendContent(F("Still scanning &mdash; <a href=\"/\">reload</a> in a moment."));
  } else {
    // Plain navigation gives no feedback until the response starts arriving,
    // which is instant on the firmware side but still a blank pause on the
    // phone. The onclick swaps the link for "Scanning..." synchronously, then
    // defers the actual navigation by one tick (setTimeout 0) so the browser
    // gets a chance to paint that text before it starts the request. No
    // external script, so this does not violate the no-external-reference
    // rule -- it never runs if JS is disabled, and the link still works, just
    // without the instant feedback.
    server_.sendContent(
        F("Network missing? <a href=\"/rescan\" id=r "
          "onclick=\"event.preventDefault();r.textContent='Scanning...';"
          "setTimeout(()=>location.href='/rescan',0)\">Scan again</a> &mdash; "
          "a scan can miss networks, and 5 GHz-only ones never appear."));
  }
  server_.sendContent(F("</p></body></html>"));
  // Zero-length chunk: terminates a chunked response. Without it the browser
  // waits for more and the page appears to hang.
  server_.sendContent(F(""));
}

void Network::appendEscaped(String& out, const String& raw) {
  // An SSID is arbitrary bytes chosen by whoever runs the access point, and it
  // is being placed into both an attribute and element text. A neighbouring
  // network named with a quote or angle bracket would otherwise break the form
  // or inject markup into it.
  for (size_t i = 0; i < raw.length(); ++i) {
    const char c = raw[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c; break;
    }
  }
}

void Network::handleSave() {
  // The dropdown's "Other" entry submits an empty value, in which case the
  // free-text field is authoritative.
  String ssid = server_.arg("s");
  if (ssid.length() == 0) ssid = server_.arg("m");
  const String password = server_.arg("p");

  if (!saveCredentials(ssid.c_str(), password.c_str())) {
    server_.send(200, "text/html",
                 F("<h1>Could not save</h1><p>Check the name is 32 characters "
                   "or fewer, then go back and try again.</p>"));
    return;
  }

  server_.send_P(200, "text/html", kSavedPage);

  // These have never been tested. If the very first attempt is rejected by the
  // AP, tick() discards them rather than leaving a Connect entry that can never
  // succeed -- the mistyped-password case.
  unproven_ = true;

  // Don't tear the AP down inside the handler -- the response still has to
  // reach the phone. tick() switches to STA mode once this state has had time
  // to flush.
  setState(NetState::kPortalSaved);
}

void Network::handleNotFound() {
  // Every unknown URL redirects to the form. This covers the OS connectivity
  // probes (Apple's /hotspot-detect.html, Android's /generate_204, Windows'
  // /connecttest.txt) without special-casing each one: none of them get the
  // response they expect, so each OS concludes it is behind a portal and opens
  // the page we point it at.
  server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(),
                     true);
  server_.send(302, "text/plain", "");
}

void Network::tick() {
  if (state_ == NetState::kOff) return;

  // Collect an async scan result before servicing HTTP, so a request arriving
  // this frame sees the freshest list.
  pollScan();

  if (server_running_) {
    dns_.processNextRequest();
    server_.handleClient();
  }

  const uint32_t elapsed = millis() - state_started_ms_;

  switch (state_) {
    case NetState::kConnecting:
      if (WiFi.status() == WL_CONNECTED) {
        // Proven. From here on a failure is environmental, not a bad password,
        // so the credentials are never discarded automatically again.
        attempt_gen_.store(kNoAttempt);
        unproven_ = false;
        fail_reason_ = FailReason::kNone;
        setState(NetState::kConnected);
      } else {
        // Wait for the deadline rather than acting the instant a rejection
        // arrives. The core retries the first disconnect internally for every
        // reason (WiFiGeneric.cpp), so aborting on the first event would cut short a retry that might well have succeeded -- and, with the
        // discard rule below, erase valid credentials over one flaky handshake.
        // The verdict is still used; it is just read at the end, by which point
        // the core has had its retry and any late-queued event has landed.
        if (elapsed > kConnectTimeoutMs) {
          failWith(classifyFailure(true));
        }
      }
      break;

    case NetState::kPortalSaved:
      // Give the browser a moment to receive the confirmation page before the
      // AP disappears out from under it.
      //
      // The second condition is a crash fix, not politeness. A scan started
      // from the portal is still owned by the SDK, and its completion callback
      // (_scanDone) runs in SDK context, allocates a bss_info array and writes
      // it into the core's static _scanResult. WiFi.scanDelete() frees the
      // *previous* buffer but does not cancel a scan in flight, so tearing the
      // soft-AP down and switching to WIFI_STA here left that callback pending
      // across a radio reconfiguration. It then fired against a stack that had
      // moved underneath it -- an Exception(0) in ctx:sys inside hostap_input,
      // with none of our code on the stack. Waiting costs at most a scan
      // period and makes the teardown deterministic.
      //
      // pollScan() at the top of tick() clears scan_pending_, so this cannot
      // deadlock: a failed scan reports WIFI_SCAN_FAILED and is collected the
      // same way a successful one is.
      if (elapsed > 1500 && !scan_pending_) {
        stopServer();
        dns_.stop();
        WiFi.softAPdisconnect(true);
        connect();
      }
      break;

    case NetState::kPortalActive:
      if (elapsed > kPortalTimeoutMs) disconnect();
      break;

    case NetState::kConnected:
      // A dropped connection should be visible, not silently pretended away.
      // Power the radio down on the way out: kFailed is a terminal state with
      // no timeout, so leaving the radio up here would strand it on until the
      // user happened to press B -- breaking the radio-off invariant on exactly
      // the path nobody tests (walking out of range, router reboot).
      //
      // kLost, never kBadPassword: these credentials demonstrably worked a
      // moment ago, so whatever happened is environmental and they are kept.
      if (WiFi.status() != WL_CONNECTED) {
        failWith(FailReason::kLost);
      }
      break;

    case NetState::kFailed:
    case NetState::kOff:
      break;
  }

  // Last thing in tick(), deliberately. This suspends the loop continuation via
  // esp_yield() and does not return until the SDK has run, so nothing may
  // follow it here -- anything that did would be skipped for the duration.
  // Guarded on the portal state so a scan requested just before the portal was
  // torn down is dropped rather than waking the radio again.
  if (state_ == NetState::kPortalActive) startRequestedScan();
}

float Network::progress() const {
  if (state_ != NetState::kConnecting) return 0.0f;
  const uint32_t elapsed = millis() - state_started_ms_;
  if (elapsed >= kConnectTimeoutMs) return 1.0f;
  return static_cast<float>(elapsed) / kConnectTimeoutMs;
}

String Network::address() const {
  if (state_ == NetState::kPortalActive || state_ == NetState::kPortalSaved) {
    return WiFi.softAPIP().toString();
  }
  if (state_ == NetState::kConnected) return WiFi.localIP().toString();
  return String();
}

}  // namespace beamboy
