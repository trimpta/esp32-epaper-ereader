#include "input.h"
#include "config.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace {

const unsigned long DEBOUNCE_MS = 30;
// Time-only lockout was too short: one TM-2024A press chatters for tens of ms, so a menu
// DU wait harvested dozens of edges and coalesced them into jump-to-first / jump-to-last.
// Arm on release (pin HIGH); one FALLING per press.
const unsigned long WHEEL_LOCKOUT_US = 20000;

struct ButtonState {
  int pin;
  bool holdCapable;
  bool stable = HIGH;
  bool lastReading = HIGH;
  unsigned long lastChangeMs = 0;
  unsigned long pressedAtMs = 0;
  bool longFired = false;
};

// MENU, EXIT, rotary click only. Up/Down are a 3-position thumbwheel, harvested by ISR.
ButtonState buttons[] = {
    {PIN_MENU_BUTTON, true},
    {PIN_EXIT_BUTTON, true},
    {PIN_ROTARY_CONF, true},
};
const size_t BUTTON_COUNT = sizeof(buttons) / sizeof(buttons[0]);
enum { BTN_MENU = 0, BTN_EXIT = 1, BTN_CONF = 2 };

InputEvent queue[8];
volatile uint8_t queueHead = 0, queueTail = 0;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile int wheelDelta = 0;
volatile uint32_t lastDownUs = 0;
volatile uint32_t lastUpUs = 0;
volatile bool downArmed = true;
volatile bool upArmed = true;

void push(InputEvent e) {
  uint8_t next = (uint8_t)((queueTail + 1) % 8);
  if (next == queueHead) return;
  queue[queueTail] = e;
  queueTail = next;
}

InputEvent pop() {
  if (queueHead == queueTail) return InputEvent::None;
  InputEvent e = queue[queueHead];
  queueHead = (uint8_t)((queueHead + 1) % 8);
  return e;
}

void harvestWheel() {
  if (digitalRead(PIN_ROTARY_DOWN) == HIGH) downArmed = true;
  if (digitalRead(PIN_ROTARY_UP) == HIGH) upArmed = true;
  int d;
  portENTER_CRITICAL(&mux);
  d = wheelDelta;
  wheelDelta = 0;
  portEXIT_CRITICAL(&mux);
  while (d > 0) {
    push(InputEvent::ScrollNext);
    d--;
  }
  while (d < 0) {
    push(InputEvent::ScrollPrev);
    d++;
  }
}

void IRAM_ATTR isrWheelDown() {
  if (!downArmed) return;
  uint32_t now = micros();
  if (now - lastDownUs < WHEEL_LOCKOUT_US) return;
  lastDownUs = now;
  downArmed = false;
  portENTER_CRITICAL_ISR(&mux);
  wheelDelta++;
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR isrWheelUp() {
  if (!upArmed) return;
  uint32_t now = micros();
  if (now - lastUpUs < WHEEL_LOCKOUT_US) return;
  lastUpUs = now;
  upArmed = false;
  portENTER_CRITICAL_ISR(&mux);
  wheelDelta--;
  portEXIT_CRITICAL_ISR(&mux);
}

InputEvent shortEventFor(size_t idx) {
  switch (idx) {
    case BTN_MENU: return InputEvent::MenuShort;
    case BTN_EXIT: return InputEvent::ExitShort;
    case BTN_CONF: return InputEvent::ConfShort;
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
    } else if (b.holdCapable && !b.longFired) {
      push(shortEventFor(idx));
    }
  }

  if (b.holdCapable && b.stable == LOW && !b.longFired &&
      (now - b.pressedAtMs) >= HOLD_GESTURE_MS) {
    b.longFired = true;
    push(longEventFor(idx));
  }
}

void pollButtons() {
  for (size_t i = 0; i < BUTTON_COUNT; i++) update(i);
}

}  // namespace

void input::begin() {
  for (auto& b : buttons) pinMode(b.pin, INPUT_PULLUP);
  pinMode(PIN_ROTARY_DOWN, INPUT_PULLUP);
  pinMode(PIN_ROTARY_UP, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ROTARY_DOWN), isrWheelDown, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_ROTARY_UP), isrWheelUp, FALLING);
}

void input::service() {
  harvestWheel();
  pollButtons();
}

InputEvent input::poll() {
  service();
  return pop();
}

bool input::anyButtonDown() {
  for (auto& b : buttons) {
    if (b.stable == LOW) return true;
  }
  return digitalRead(PIN_ROTARY_DOWN) == LOW || digitalRead(PIN_ROTARY_UP) == LOW;
}

const int kWakePins[] = {PIN_MENU_BUTTON, PIN_EXIT_BUTTON, PIN_ROTARY_CONF, PIN_ROTARY_DOWN,
                         PIN_ROTARY_UP};
const size_t kWakePinCount = sizeof(kWakePins) / sizeof(kWakePins[0]);

bool anyWakePinLow() {
  for (size_t i = 0; i < kWakePinCount; i++) {
    if (digitalRead(kWakePins[i]) == LOW) return true;
  }
  return false;
}

void waitWakePinsHigh() {
  while (anyWakePinLow()) {
    delay(10);
    harvestWheel();
    pollButtons();
  }
  delay(40);
  portENTER_CRITICAL(&mux);
  wheelDelta = 0;
  downArmed = true;
  upArmed = true;
  portEXIT_CRITICAL(&mux);
  while (pop() != InputEvent::None) {
  }
}

// The wheel pins are FALLING-edge ISRs, and no ISR runs while the CPU is halted — so a
// pulse that arrives during light sleep leaves the pin low with no edge ever recorded.
// Re-derive it from the level on wake. The armed flags make this safe against
// double-counting: if the ISR did manage to fire, it already cleared them.
void syncWheelAfterSleep() {
  uint32_t now = micros();
  if (downArmed && digitalRead(PIN_ROTARY_DOWN) == LOW) {
    downArmed = false;
    lastDownUs = now;
    portENTER_CRITICAL(&mux);
    wheelDelta++;
    portEXIT_CRITICAL(&mux);
  }
  if (upArmed && digitalRead(PIN_ROTARY_UP) == LOW) {
    upArmed = false;
    lastUpUs = now;
    portENTER_CRITICAL(&mux);
    wheelDelta--;
    portEXIT_CRITICAL(&mux);
  }
}

// Arms every key as a light-sleep wake source, sleeps, disarms. Shared by both sleep
// paths; neither the wheel re-derivation nor the drain-the-click policy lives here.
bool sleepArmed(unsigned long maxMs) {
  size_t armed = 0;
  bool ok = true;
  for (size_t i = 0; i < kWakePinCount; i++) {
    if (gpio_wakeup_enable((gpio_num_t)kWakePins[i], GPIO_INTR_LOW_LEVEL) != ESP_OK) {
      ok = false;
      break;
    }
    armed++;
  }
  if (ok && esp_sleep_enable_gpio_wakeup() != ESP_OK) ok = false;
  if (!ok) {
    for (size_t i = 0; i < armed; i++) gpio_wakeup_disable((gpio_num_t)kWakePins[i]);
    return false;
  }
  if (maxMs > 0) esp_sleep_enable_timer_wakeup((uint64_t)maxMs * 1000ULL);
  esp_light_sleep_start();
  if (maxMs > 0) esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  for (size_t i = 0; i < kWakePinCount; i++) {
    gpio_wakeup_disable((gpio_num_t)kWakePins[i]);
  }
  return true;
}

bool input::enterLightSleepUntilInput() {
  return sleepArmed(0);
}

bool input::dozeUntilInput(unsigned long maxMs) {
  // A key already down means there's an in-flight gesture (a hold, mid-debounce) whose
  // timing the caller still needs to poll — sleeping through it would stall it.
  if (anyWakePinLow() || queueHead != queueTail) return false;
  if (!sleepArmed(maxMs)) return false;
  syncWheelAfterSleep();
  return true;
}

void input::blockUntilWake() {
  waitWakePinsHigh();
  if (!enterLightSleepUntilInput()) {
    for (;;) {
      harvestWheel();
      pollButtons();
      if (anyWakePinLow() || queueHead != queueTail) break;
      delay(20);
    }
  }
  waitWakePinsHigh();
}
