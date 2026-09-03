#pragma once
// The on-device screen stack and menu flow. This is the firmware counterpart to the
// simulator's LIST_SCREENS registry + doMenu()/doExit()/doNext() dispatch — same screens,
// same two-layer menu (MENU opens the current book's own menu, EXIT opens Home), same six
// gestures. docs/SIMULATOR.md is the reference for the intended behavior.

#include <Arduino.h>

#include "input.h"

class Renderer;

namespace ui {
void begin(Renderer* renderer);

// Feed every input event here; the UI decides what the current screen does with it.
void handle(InputEvent e);

// Call every loop(): idle-sleep timing, reading-time accounting, debounced state flush.
void tick();

// A book finished uploading over WiFi — rescan and, if the reader is sitting on an empty
// library, show it immediately rather than waiting for a reboot.
void onLibraryChanged();

void showMessage(const String& line);
}  // namespace ui
