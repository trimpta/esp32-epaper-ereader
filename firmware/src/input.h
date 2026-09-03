#pragma once
// Debounced button/rotary polling. Pins from config.h (Elecrow's published GPIO table).
//
// Three physical inputs (rotary encoder + MENU + EXIT), six gestures — the same set the
// simulator wires up (see docs/SIMULATOR.md "Input model"). The rotary's UP/DOWN lines are
// momentary detent pulses, so they only ever produce a scroll event; its push-click (CONF)
// and the two buttons each distinguish a short press from a hold.

enum class InputEvent {
  None,
  ScrollNext,   // rotary detent, "down"
  ScrollPrev,   // rotary detent, "up"
  ConfShort,    // rotary push-click
  ConfLong,     // rotary held — enters scrub-edit on a numeric field
  MenuShort,
  MenuLong,     // force a full refresh now, bypassing the cadence counter
  ExitShort,
  ExitLong,     // sleep immediately
};

namespace input {
void begin();

// Call every loop() iteration — non-blocking. Returns at most one event per call, but
// polls every button every call, so a second button changing state in the same tick is
// queued rather than dropped.
InputEvent poll();

// True while any button is physically down — used to hold off idle sleep mid-gesture.
bool anyButtonDown();

// TODO(verify): ext1 wakeup requires RTC-capable GPIOs on the ESP32-S3, and the RTC
// IO map differs from the original ESP32 — confirm IO1/2/4/5/6 (config.h) actually
// support ext1 wake before relying on this instead of the simple poll-and-delay loop
// main.cpp uses by default. Returns false if the pin set isn't usable, so the caller
// can fall back to polling rather than sleeping and never waking.
bool enterLightSleepUntilInput();
}  // namespace input
