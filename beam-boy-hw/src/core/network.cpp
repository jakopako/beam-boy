#include "network.h"

#include <LittleFS.h>

namespace beamboy {
namespace {

constexpr char kCredentialsPath[] = "/net.cfg";
constexpr char kApName[] = "BeamBoy-Setup";

// The setup page. Deliberately one self-contained string with no external CSS,
// fonts or scripts: the portal has no internet route, so any external reference
// would hang until it times out. Kept in PROGMEM to keep it out of RAM on the
// ESP8266, where it would otherwise cost ~1.5 KB of the ~46 KB we have.
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

  // Make sure a previous session (or the bootloader's persisted config) has not
  // left the radio on. Arduino cores persist WiFi settings in flash and will
  // auto-reconnect at boot unless told not to -- which would violate the
  // radio-off-by-default rule before our code ever runs.
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
#if defined(ARDUINO_ARCH_ESP8266)
  WiFi.setAutoConnect(false);
#endif
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

  // If the radio is currently using the credentials being erased, take it down
  // too. Otherwise "forget" leaves the device still associated to the network it
  // claims to have forgotten -- and still drawing power for it -- until the next
  // reboot, which is not what the user asked for.
  if (state_ != NetState::kOff) disconnect();
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
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_, password_);
  setState(NetState::kConnecting);
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
  runScan();

  WiFi.softAP(kApName);

  // Resolve every hostname to us. This is what makes the phone's captive-portal
  // check fail in the specific way that pops the login sheet: the OS requests a
  // known URL, DNS points it here, and we answer with something other than the
  // expected 204/success body.
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  // DNSServer::start() takes its port by reference, which odr-uses the constant
  // and would demand an out-of-line definition (a link error on ESP32, though
  // not on ESP8266 where it gets inlined away). Copying to a local sidesteps it.
  uint16_t dns_port = kDnsPort;
  dns_.start(dns_port, "*", WiFi.softAPIP());

  startServer();
  setState(NetState::kPortalActive);
}

void Network::disconnect() {
  stopServer();
  dns_.stop();

  // Scan results live in memory owned by the WiFi driver until explicitly
  // deleted. Leaving them allocated would leak a few hundred bytes per portal
  // session in an object that is never destroyed -- the same shape as the
  // handler leak found in review, and it would surface the same way, as a TLS
  // handshake failing later for want of contiguous heap.
  WiFi.scanDelete();
  scan_count_ = 0;
  scan_pending_ = false;

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

void Network::runScan() {
  // Asynchronous, and this is not optional. A synchronous scan blocks for 1-2 s
  // on the ESP8266 and up to ~6.5 s on the ESP32 (13 channels x 500 ms dwell).
  // Blocking that long anywhere is against this file's non-blocking rule, but
  // doing it inside an HTTP handler is worse: the ESP8266's sync scan yields via
  // esp_suspend(), which resumes the main loop continuation, which calls tick(),
  // which calls server_.handleClient() again -- re-entering the web server while
  // one of its own handlers is still on the stack. The ESP32 instead risks the
  // task watchdog firing. Either way the tube freezes for the whole scan.
  //
  // show_hidden = true: a hidden AP reports an empty SSID, which is filtered out
  // when rendering, but asking costs nothing.
#if defined(ARDUINO_ARCH_ESP8266)
  WiFi.scanNetworks(true, true);
#else
  // ESP32 exposes the per-channel dwell. The default is 300 ms; 500 gives
  // slow-beaconing APs another chance to be heard. Async, so the cost is paid in
  // wall-clock time rather than in a stalled loop.
  WiFi.scanNetworks(true, true, false, 500);
#endif
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
  Serial.println(scan_count_);
}

void Network::handleRescan() {
  // Respond first, scan afterwards. The scan takes seconds and must not happen
  // inside this handler (see runScan), so the reply is a short page that waits
  // and reloads itself. That also gives the user something to look at, which a
  // silently-stalled form would not.
  server_.send_P(200, "text/html", kScanningPage);
  runScan();
}

void Network::handleRoot() {
  // The page is assembled in one String rather than streamed, because the
  // ESP8266 server closes the connection between chunks unless content-length is
  // known. A scan list of ~20 networks is well under a kilobyte.
  String page;
  page.reserve(2048);
  page += FPSTR(kSetupPageHead);

  // Build the options first. A rescan frees the previous result buffer the
  // moment it starts, so scan_count_ can still be non-zero while every entry
  // reads back empty. Deciding between the dropdown and the plain text field on
  // the *rendered* result rather than on the count avoids emitting a <select>
  // containing nothing but "Other".
  String options;
  if (scan_count_ > 0 && !scan_pending_) {
    options.reserve(1024);
    for (int8_t i = 0; i < scan_count_; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;  // Hidden network: nothing to show.
      options += F("<option value=\"");
      appendEscaped(options, ssid);
      options += F("\">");
      appendEscaped(options, ssid);
      // Signal strength, so it is obvious which of two similar names is yours.
      options += WiFi.RSSI(i) >= -67 ? F(" &middot;&middot;&middot;") : F(" &middot;");
      options += F("</option>");
    }
  }

  if (options.length() > 0) {
    page += F("<select id=s name=s>");
    page += options;
    // A network can be hidden, or 5 GHz-only and thus invisible to this chip, so
    // there must always be a way to type a name the scan never found.
    page += F("<option value=\"\">Other (type below)</option></select>"
              "<label for=m>Or type the name</label>"
              "<input id=m name=m maxlength=32>");
  } else {
    page += FPSTR(kSetupPageManual);
  }

  page += FPSTR(kSetupPageTail);

  // A single scan genuinely misses access points, so an incomplete list is
  // expected rather than exceptional -- say so where it will be read after the
  // user fails to find their network, and make retrying one tap.
  page += F("<p class=hint>");
  if (scan_pending_) {
    // Distinguishes "still looking" from "looked and found nothing". Without
    // this, a form loaded while the first scan is still running looks like a
    // scan that failed.
    page += F("Still scanning &mdash; <a href=\"/\">reload</a> in a moment.");
  } else {
    page += F("Network missing? <a href=\"/rescan\">Scan again</a> &mdash; "
              "a scan can miss networks, and 5 GHz-only ones never appear.");
  }
  page += F("</p></body></html>");

  server_.send(200, "text/html", page);
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
        setState(NetState::kConnected);
      } else if (elapsed > kConnectTimeoutMs) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        setState(NetState::kFailed);
      }
      break;

    case NetState::kPortalSaved:
      // Give the browser a moment to receive the confirmation page before the
      // AP disappears out from under it.
      if (elapsed > 1500) {
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
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        setState(NetState::kFailed);
      }
      break;

    case NetState::kFailed:
    case NetState::kOff:
      break;
  }
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
