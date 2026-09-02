#include "input.h"
#include "config.h"

#include <Arduino.h>
#include <esp_sleep.h>

namespace {

struct ButtonState {
  int pin;
  bool stableState = HIGH;
  bool lastReading = HIGH;
  unsigned long lastChangeMs = 0;
};

const unsigned long DEBOUNCE_MS = 30;

// Order matches the InputEvent each one maps to in poll().
ButtonState buttons[] = {
    {PIN_MENU_BUTTON},
    {PIN_EXIT_BUTTON},
    {PIN_ROTARY_DOWN},  // -> PageNext
    {PIN_ROTARY_UP},    // -> PagePrev
    {PIN_ROTARY_CONF},  // -> Confirm
};

// Returns true on the debounced falling edge (buttons are active-low w/ INPUT_PULLUP).
bool checkPress(ButtonState& b) {
  bool reading = digitalRead(b.pin);
  unsigned long now = millis();
  if (reading != b.lastReading) {
    b.lastChangeMs = now;
    b.lastReading = reading;
  }
  bool fired = false;
  if ((now - b.lastChangeMs) > DEBOUNCE_MS && b.stableState != b.lastReading) {
    b.stableState = b.lastReading;
    if (b.stableState == LOW) fired = true;
  }
  return fired;
}

}  // namespace

void input::begin() {
  for (auto& b : buttons) pinMode(b.pin, INPUT_PULLUP);
}

InputEvent input::poll() {
  if (checkPress(buttons[0])) return InputEvent::Menu;
  if (checkPress(buttons[1])) return InputEvent::Exit;
  if (checkPress(buttons[2])) return InputEvent::PageNext;
  if (checkPress(buttons[3])) return InputEvent::PagePrev;
  if (checkPress(buttons[4])) return InputEvent::Confirm;
  return InputEvent::None;
}

void input::enterLightSleepUntilInput() {
  uint64_t mask = 0;
  for (auto& b : buttons) mask |= (1ULL << b.pin);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
  esp_light_sleep_start();
}
