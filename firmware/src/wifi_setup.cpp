#include "wifi_setup.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <algorithm>

namespace {
WiFiManager wm;
unsigned long lastReconnectAttempt = 0;
unsigned long reconnectBackoffMs = 2000;
const unsigned long MAX_BACKOFF_MS = 60000;
}  // namespace

bool wifi_setup::begin(bool forcePortal, std::function<void(const String&)> onIpKnown) {
  // 3 minute timeout so an accidental MENU-hold at boot doesn't strand the device in
  // AP mode indefinitely — it just falls through and retries the saved network.
  wm.setConfigPortalTimeout(180);

  bool ok = forcePortal ? wm.startConfigPortal(WIFI_AP_NAME) : wm.autoConnect(WIFI_AP_NAME);

  if (ok && onIpKnown) onIpKnown(WiFi.localIP().toString());
  return ok;
}

void wifi_setup::poll() {
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
