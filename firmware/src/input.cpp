#include "input.h"
#include "config.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

namespace {

const unsigned long DEBOUNCE_MS = 30;

// Buttons that distinguish short from long presses need release-edge detection too, so
// this tracks the full press/release cycle rather than only the falling edge.
struct ButtonState {
  int pin;
  bool holdCapable;
  bool stable = HIGH;
  bool lastReading = HIGH;
  unsigned long lastChangeMs = 0;
  unsigned long pressedAtMs = 0;
  bool longFired = false;
};

ButtonState buttons[] = {
    {PIN_MENU_BUTTON, true},
    {PIN_EXIT_BUTTON, true},
    {PIN_ROTARY_CONF, true},
    {PIN_ROTARY_DOWN, false},  // detent pulse -> ScrollNext
    {PIN_ROTARY_UP, false},    // detent pulse -> ScrollPrev
};
const size_t BUTTON_COUNT = sizeof(buttons) / sizeof(buttons[0]);
enum { BTN_MENU = 0, BTN_EXIT = 1, BTN_CONF = 2, BTN_DOWN = 3, BTN_UP = 4 };

// Small ring buffer: every button is polled every tick (so a second one changing state in
// the same tick isn't dropped the way an early `return` from poll() used to drop it), but
// callers still consume one event at a time.
InputEvent queue[8];
uint8_t queueHead = 0, queueTail = 0;

void push(InputEvent e) {
  uint8_t next = (uint8_t)((queueTail + 1) % 8);
  if (next == queueHead) return;  // full — drop rather than overwrite an unread event
  queue[queueTail] = e;
  queueTail = next;
}

InputEvent pop() {
  if (queueHead == queueTail) return InputEvent::None;
  InputEvent e = queue[queueHead];
  queueHead = (uint8_t)((queueHead + 1) % 8);
  return e;
}

InputEvent shortEventFor(size_t idx) {
  switch (idx) {
    case BTN_MENU: return InputEvent::MenuShort;
    case BTN_EXIT: return InputEvent::ExitShort;
    case BTN_CONF: return InputEvent::ConfShort;
    case BTN_DOWN: return InputEvent::ScrollNext;
    case BTN_UP: return InputEvent::ScrollPrev;
  }
  return InputEvent::None;
}

InputEvent longEventFor(size_t idx) {
  switch (idx) {
    case BTN_MENU: return InputEvent::MenuLong;
    case BTN_EXIT: return InputEvent::ExitLong;
    case BTN_CONF: return InputEvent::ConfLong;
  }
  return InputEvent::None;
}

// Buttons are active-low (INPUT_PULLUP). Emits the short event on release-before-threshold
// and the long event the moment the threshold is crossed while still held — same contract
// as the simulator's bindHoldGesture().
void update(size_t idx) {
  ButtonState& b = buttons[idx];
  bool reading = digitalRead(b.pin);
  unsigned long now = millis();

  if (reading != b.lastReading) {
    b.lastChangeMs = now;
    b.lastReading = reading;
  }

  if ((now - b.lastChangeMs) > DEBOUNCE_MS && b.stable != b.lastReading) {
    b.stable = b.lastReading;
    if (b.stable == LOW) {
      b.pressedAtMs = now;
      b.longFired = false;
      if (!b.holdCapable) push(shortEventFor(idx));  // detents fire immediately
    } else if (b.holdCapable && !b.longFired) {
      push(shortEventFor(idx));  // released before the hold threshold
    }
  }

  if (b.holdCapable && b.stable == LOW && !b.longFired &&
      (now - b.pressedAtMs) >= HOLD_GESTURE_MS) {
    b.longFired = true;
    push(longEventFor(idx));
  }
}

}  // namespace

void input::begin() {
  for (auto& b : buttons) pinMode(b.pin, INPUT_PULLUP);
}

InputEvent input::poll() {
  for (size_t i = 0; i < BUTTON_COUNT; i++) update(i);
  return pop();
}

bool input::anyButtonDown() {
  for (auto& b : buttons) {
    if (b.stable == LOW) return true;
  }
  return false;
}

bool input::enterLightSleepUntilInput() {
  uint64_t mask = 0;
  for (auto& b : buttons) {
    if (!esp_sleep_is_valid_wakeup_gpio((gpio_num_t)b.pin)) return false;
    mask |= (1ULL << b.pin);
  }
  // ANY_LOW, not ALL_LOW: the buttons are active-low and independent, so requiring all of
  // them to be low at once (as this did before) meant the device could only be woken by
  // holding every button down simultaneously. ESP32-S3 supports the any-low mode; the
  // original ESP32 does not, hence the guard.
#if SOC_PM_SUPPORT_EXT1_WAKEUP
#if CONFIG_IDF_TARGET_ESP32
  // The original ESP32 only offers ALL_LOW/ANY_HIGH, so any-of-several-active-low buttons
  // isn't expressible there; this board isn't that chip, but keep the build honest.
  const esp_sleep_ext1_wakeup_mode_t mode = ESP_EXT1_WAKEUP_ALL_LOW;
#else
  const esp_sleep_ext1_wakeup_mode_t mode = ESP_EXT1_WAKEUP_ANY_LOW;
#endif
  if (esp_sleep_enable_ext1_wakeup(mask, mode) != ESP_OK) return false;
  esp_light_sleep_start();
  return true;
#else
  return false;
#endif
}
