#include "wifi_setup.h"
#include "web_server.h"
#include "config.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <algorithm>

// This used to be built on WiFiManager for both the AP/captive-portal path and STA
// credential entry. That doesn't work anymore now that the AP is a normal, always-
// reachable-from-the-menu transfer path rather than a momentary setup screen: WiFiManager
// runs its own web server for as long as its config portal is open, and web_server.cpp's
// uploader needs port 80 too — the two could never be up at once (the old code even had to
// explicitly stop one before starting the other). Rather than keep WiFiManager around just
// for its STA-credential form, this brings the AP up directly with WiFi.softAP() + a
// DNSServer, and serves the join form itself from the one AsyncWebServer web_server.cpp
// already owns (POST /wifi/join — see there). WiFi.begin(ssid, pass) persists credentials
// to NVS on its own (the ESP32 SDK's job, not ours), which is all WiFiManager was really
// storing for us — so enable()'s bare WiFi.begin() reconnect keeps working unchanged.

namespace {
bool radioWanted = false;
bool apWanted = false;

DNSServer dns;
bool dnsRunning = false;
const byte DNS_PORT = 53;

unsigned long lastReconnectAttempt = 0;
unsigned long reconnectBackoffMs = 2000;
const unsigned long MAX_BACKOFF_MS = 60000;

void stopDns() {
  if (!dnsRunning) return;
  dns.stop();
  dnsRunning = false;
}
}  // namespace

void wifi_setup::begin() {
  radioWanted = false;
  apWanted = false;
  WiFi.mode(WIFI_OFF);
}

void wifi_setup::enable() {
  if (apWanted) stopAP();
  radioWanted = true;
  lastReconnectAttempt = 0;
  reconnectBackoffMs = 2000;
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  web_server::start();
}

void wifi_setup::disable() {
  stopDns();
  radioWanted = false;
  apWanted = false;
  web_server::stop();
  WiFi.mode(WIFI_OFF);
}

bool wifi_setup::radioOn() { return radioWanted; }

bool wifi_setup::isConnected() {
  return radioWanted && !apWanted && WiFi.status() == WL_CONNECTED;
}

String wifi_setup::ipAddress() {
  return WiFi.localIP().toString();
}

void wifi_setup::startAP() {
  if (radioWanted) disable();  // clean slate — don't layer AP over a live STA session
  radioWanted = true;
  apWanted = true;
  WiFi.mode(WIFI_AP);
  // softAP()'s default gateway/DHCP pool already puts the AP at 192.168.4.1
  // (config.h's WIFI_AP_IP) — no softAPConfig() call needed.
  WiFi.softAP(WIFI_AP_NAME, WIFI_AP_PASSWORD);
  dns.start(DNS_PORT, "*", WiFi.softAPIP());
  dnsRunning = true;
  web_server::start();
}

void wifi_setup::stopAP() {
  if (!apWanted) return;
  stopDns();
  apWanted = false;
  radioWanted = false;
  web_server::stop();
  WiFi.mode(WIFI_OFF);
}

bool wifi_setup::apOpen() { return apWanted; }

bool wifi_setup::joinNetwork(const String& ssid, const String& pass) {
  if (!ssid.length()) return false;
  // Drop the AP's DNS redirect — it would keep answering every lookup with 192.168.4.1
  // while we're trying to join a real network. web_server itself stays up: it doesn't
  // care which radio mode it's riding on, and switching it off here would drop the
  // request this join came in on.
  stopDns();
  apWanted = false;
  radioWanted = true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
  web_server::start();
  return true;  // join is async — caller polls isConnected()/ipAddress()
}

String wifi_setup::apQrPayload() {
  // WPA2-PSK format per the widely-supported (Android/iOS stock camera) WIFI: QR
  // convention. No escaping of the reserved ; , \ " chars needed — WIFI_AP_NAME and
  // WIFI_AP_PASSWORD (config.h) are fixed compile-time strings with none of them.
  return String("WIFI:S:") + WIFI_AP_NAME + ";T:WPA;P:" + WIFI_AP_PASSWORD + ";;";
}

String wifi_setup::apCaption() {
  return String(WIFI_AP_NAME) + "\nPassword:\n" + WIFI_AP_PASSWORD + "\n\nOr open\n" + WIFI_AP_IP;
}

String wifi_setup::staQrPayload() {
  return String("http://") + WiFi.localIP().toString() + "/";
}

void wifi_setup::poll() {
  if (dnsRunning) dns.processNextRequest();
  if (apWanted || !radioWanted) return;
  if (WiFi.status() == WL_CONNECTED) {
    reconnectBackoffMs = 2000;
    return;
  }
  unsigned long now = millis();
  if (now - lastReconnectAttempt < reconnectBackoffMs) return;
  lastReconnectAttempt = now;
  WiFi.reconnect();
  reconnectBackoffMs = std::min(reconnectBackoffMs * 2, MAX_BACKOFF_MS);
}
