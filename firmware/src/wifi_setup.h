#pragma once
// Wi-Fi is opt-in. The device is an offline reader; the radio exists so you can join a
// hotspot (station mode, for the desk workflow) or join the reader's own AP (its normal
// book-transfer path — see docs/ARCHITECTURE.md "Power"/"Transport") to upload a .cebk.
//
// No WiFiManager: it ran its own web server for the whole config-portal lifetime, which
// can never share port 80 with web_server.cpp's uploader — see the removal note at the
// top of wifi_setup.cpp. The AP here is brought up directly with WiFi.softAP() + a
// DNSServer captive-portal redirect, and everything (uploader, wifi-join form, captive
// probe responses) is served by the one AsyncWebServer web_server.cpp already owns.

#include <Arduino.h>

namespace wifi_setup {

void begin();

// Station mode: join with previously-saved credentials (WiFi.begin() with no args reuses
// whatever WiFi.begin(ssid,pass) last persisted to NVS — the ESP32 SDK does this on its
// own, nothing here manages that storage).
void enable();
// Turns off whichever mode is active (STA or AP) and stops the shared web server.
void disable();

bool radioOn();     // STA or AP is up
bool isConnected(); // STA joined a network
String ipAddress(); // STA's address once connected

// The AP is long-lived once started — not a portal that tears itself down — so ui::tick's
// idle checks and web_server's lifetime can both just ask "is it up".
void startAP();
void stopAP();
bool apOpen();

// Attempts to join `ssid`/`pass` from STA mode; used by the wifi-join page served while
// the AP is up. Persists via the SDK's own WiFi.begin() persistence, same as enable().
// Does not itself tear down the AP — the caller decides when (see web_server.cpp).
bool joinNetwork(const String& ssid, const String& pass);

// QR payload/caption for the AP-join screen: the standard WIFI:S:...;T:WPA;P:...;; format
// phone cameras recognize, and a human-readable caption (SSID + password) as a fallback
// for a camera that won't scan. Both are compile-time constants under config.h's
// WIFI_AP_NAME/WIFI_AP_PASSWORD — valid any time, not just while the AP is actually up.
String apQrPayload();
String apCaption();

// QR payload for "open this URL" once connected in station mode. Only meaningful while
// isConnected() is true.
String staQrPayload();

void poll();  // DNS + STA reconnect-with-backoff; call every loop()

}  // namespace wifi_setup
