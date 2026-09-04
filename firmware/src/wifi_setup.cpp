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

  // Without an explicit connect timeout, autoConnect()'s attempt at a saved network
  // that's out of range or gone (confirmed on real hardware: a stale credential left
  // over from before this board reached its owner, since flashing app/bootloader/
  // partitions never touches the separate NVS partition WiFiManager's saved
  // credentials live in) hung long enough to trip the watchdog and reboot the device,
  // in a loop, before it ever reached the AP-mode fallback below. 15s is enough for a
  // real network to associate; past that, fall through to the portal instead of
  // hanging.
  wm.setConnectTimeout(15);

  bool ok = forcePortal ? wm.startConfigPortal(WIFI_AP_NAME) : wm.autoConnect(WIFI_AP_NAME);

  if (ok) {
    // There's no RTC on this board, so reading stats have no calendar day to attribute
    // pages to until this lands. Anything read before it goes into library.cpp's day-0
    // bucket rather than being filed under a made-up date.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    if (onIpKnown) onIpKnown(WiFi.localIP().toString());
  }
  return ok;
}

bool wifi_setup::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String wifi_setup::ipAddress() {
  return WiFi.localIP().toString();
}

void wifi_setup::startPortal() {
  wm.setConfigPortalTimeout(180);
  wm.startConfigPortal(WIFI_AP_NAME);
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
