#pragma once
// Persistent STA WiFi (phone hotspot or home router — same code path either way),
// not a per-session AP the user has to switch to. See docs/ARCHITECTURE.md
// "Transport". The device never assumes mDNS works (unreliable from Android
// browsers), so callers should show the returned IP directly on-screen.

#include <Arduino.h>
#include <functional>

namespace wifi_setup {

// forcePortal: true only when the user held MENU at boot, to reconfigure WiFi.
// Otherwise connects with saved credentials, falling back to a captive portal
// only if none are saved yet.
bool begin(bool forcePortal, std::function<void(const String&)> onIpKnown);

// Call every loop() iteration. Retry-with-backoff reconnect — phone hotspots sleep
// after inactivity and come back with the same SSID/password, so this never
// re-enters the captive portal on its own.
void poll();

}  // namespace wifi_setup
