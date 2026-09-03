#include <Arduino.h>
#include <LittleFS.h>
#include <esp_random.h>

#include "battery.h"
#include "config.h"
#include "input.h"
#include "library.h"
#include "renderer.h"
#include "settings.h"
#include "ui.h"
#include "web_server.h"
#include "wifi_setup.h"

namespace {
Renderer renderer;
}  // namespace

void setup() {
  Serial.begin(115200);
  input::begin();
  LittleFS.begin(true);
  settings::begin();
  renderer.begin();
  randomSeed(esp_random());  // games otherwise deal the same "random" board every boot

  // Hold MENU while powering on to re-enter WiFi setup.
  bool forcePortal = (digitalRead(PIN_MENU_BUTTON) == LOW);

  renderer.renderStatusLine("Connecting to WiFi...");
  wifi_setup::begin(forcePortal, [](const String& ip) {
    // Printed directly rather than relying on ereader.local — see wifi_setup.h.
    String status = "Connect at: " + ip;
    int pct = battery::readPercent();
    // -1 until PIN_BATTERY_ADC in config.h is wired up — see battery.h.
    if (pct >= 0) status += "   Batt: " + String(pct) + "%";
    renderer.renderStatusLine(status);
    delay(2000);
  });

  web_server::begin([]() { ui::onLibraryChanged(); });

  library::begin();
  ui::begin(&renderer);
}

void loop() {
  wifi_setup::poll();

  // Drain the input queue rather than handling one event per iteration, so a fast
  // double-press isn't paced by the loop delay below.
  for (InputEvent e = input::poll(); e != InputEvent::None; e = input::poll()) {
    ui::handle(e);
  }

  ui::tick();

  // TODO(verify): once the ext1 wakeup pin set is confirmed on real hardware (input.h),
  // ui::tick()'s sleep path can hand off to input::enterLightSleepUntilInput() instead of
  // this poll-and-delay loop, which is what actually saves power between page turns.
  delay(20);
}
