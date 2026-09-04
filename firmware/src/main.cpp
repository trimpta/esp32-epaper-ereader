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

  // Nothing drives this pin unless firmware does — the board doesn't light it on its
  // own just from having power. On solid for as long as the device is running.
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, HIGH);

  input::begin();
  // LittleFS::begin()'s partitionLabel defaults to "spiffs" — a holdover from when the
  // library shared code with SPIFFS. firmware/partitions.csv labels the data partition
  // "littlefs" (see its own header comment), so the default silently mounted nothing:
  // LittleFS.begin() would find no matching partition and every file operation failed,
  // undetectable by a compile and invisible in the simulator, which never touches real
  // flash — this only ever showed up running on actual hardware.
  LittleFS.begin(true, "/littlefs", 10, "littlefs");
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

  // library:: and ui:: must be ready before the async server starts: an upload landing
  // between web_server::begin() and ui::begin() used to call ui::onLibraryChanged() ->
  // renderCurrent() while the renderer pointer was still null.
  library::begin();
  ui::begin(&renderer);
  web_server::begin([]() { ui::onLibraryChanged(); });
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
