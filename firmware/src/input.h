#pragma once
// Debounced button/rotary polling. Pins from config.h (Elecrow's published GPIO table).
//
// Three physical inputs (thumbwheel + MENU + EXIT), six gestures — the same set the
// simulator wires up (see docs/SIMULATOR.md "Input model"). The wheel is an XKB TM-2024A
// (up / center / down + push), not a quadrature encoder: UP/DOWN are momentary pulses.
// Those two pins are edge-ISRs; CONF/MENU/EXIT are polled with a short/long split.

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

// Harvest wheel ISRs + debounce MENU/EXIT/CONF without popping. Call from long waits
// (e-paper busy) so clicks during a refresh are not dropped.
void service();

// True while any button is physically down — used to hold off idle sleep mid-gesture.
bool anyButtonDown();

// Wait for all keys up, light-sleep until a press, drain the wake click so it does
// not also act as a menu command. Falls back to polling if GPIO wake is unavailable.
// This is the sleep-screen path: the press that wakes you is spent on waking.
void blockUntilWake();

// The between-keystrokes path. Light-sleeps until a key goes down or maxMs elapses, and
// unlike blockUntilWake() it does NOT swallow the waking press — it re-derives the wheel
// pulse that the halted ISRs missed and leaves it queued, so the next poll() turns it into
// a normal event. That's what makes dozing invisible: a page turn still costs one press.
// Returns false without sleeping if a key is already down or GPIO wake isn't available,
// so the caller can fall back to its normal poll-and-delay loop.
bool dozeUntilInput(unsigned long maxMs);

// Light-sleep until any key goes low, swallowing nothing and re-deriving nothing — the
// raw primitive both paths above are built on. Note this is GPIO light-sleep wake, not
// ext1: it works on any GPIO, so unlike deep-sleep wake it doesn't care whether
// IO1/2/4/5/6 are RTC-capable. Still returns false if arming fails, so every caller can
// fall back to polling rather than sleeping and never waking.
bool enterLightSleepUntilInput();
}  // namespace input
