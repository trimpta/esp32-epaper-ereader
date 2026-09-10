#include <Arduino.h>
#include <LittleFS.h>
#include <esp_random.h>

#include "config.h"
#include "input.h"
#include "library.h"
#include "qr_screen.h"
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

  wifi_setup::begin();  // radio off — reading must not wait on a network

  library::begin();
  ui::begin(&renderer);
  web_server::begin([]() { ui::onLibraryChanged(); }, []() { ui::onWallpapersChanged(); });

  // Hold MENU at power-on as a secondary way into the transfer AP — for a first boot or
  // a dead battery, before there's any menu to navigate to "Connect / Transfer books".
  if (digitalRead(PIN_MENU_BUTTON) == LOW) {
    wifi_setup::startAP();
    qr_screen::show(renderer, wifi_setup::apQrPayload(), wifi_setup::apCaption());
    ui::onLibraryChanged();
  }
}

void loop() {
  wifi_setup::poll();

  // Drain the input queue rather than handling one event per iteration, so a fast
  // double-press isn't paced by the loop delay below.
  InputEvent e = input::poll();
  while (e != InputEvent::None) {
    if (e == InputEvent::ScrollNext || e == InputEvent::ScrollPrev) {
      int scroll = 0;
      while (e == InputEvent::ScrollNext || e == InputEvent::ScrollPrev) {
        scroll += (e == InputEvent::ScrollNext) ? 1 : -1;
        e = input::poll();
      }
      ui::handleScroll(scroll);
      continue;
    }
    ui::handle(e);
    e = input::poll();
  }

  ui::tick();

  // ui::tick() light-sleeps on its own once the keys go quiet (config.h, stage 1), so
  // this delay only paces the short awake window right after a press — the idle case
  // isn't spinning here.
  delay(20);
}
