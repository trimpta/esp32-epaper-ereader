#pragma once
// Debounced button/rotary polling. Pins from config.h (Elecrow's published GPIO table).

enum class InputEvent { None, PageNext, PagePrev, Confirm, Menu, Exit };

namespace input {
void begin();

// Call every loop() iteration — non-blocking, returns at most one edge-triggered
// event per call.
InputEvent poll();

// TODO(verify): ext1 wakeup requires RTC-capable GPIOs on the ESP32-S3, and the RTC
// IO map differs from the original ESP32 — confirm IO1/2/4/5/6 (config.h) actually
// support ext1 wake before relying on this instead of the simple poll-and-delay loop
// main.cpp uses by default.
void enterLightSleepUntilInput();
}  // namespace input
