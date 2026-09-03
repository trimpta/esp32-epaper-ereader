#pragma once
// The handful of things that are genuinely adjustable at runtime, persisted in NVS.
//
// Deliberately NOT here: margin, line height, heading height and font size. Those are
// baked into every .cebk at conversion time (docs/FORMAT.md) — changing one after books
// are on the device would desync their precomputed line/page tables from what gets drawn.
// The simulator's Layout Parameters sliders are a design-time tool for picking the values
// to hardcode in config.h, not a reader-facing setting; docs/SIMULATOR.md spells this out.

#include <Arduino.h>

namespace settings {
void begin();

// Full refresh cadence, in page turns. Safe to change at runtime: it affects only how
// often ghosting gets cleared, never how text is laid out.
int refreshEveryNPages();
void setRefreshEveryNPages(int n);
static const int REFRESH_MIN = 2;
static const int REFRESH_MAX = 20;

// Index into whatever wallpaper::count() found in /wallpapers; -1 = none/blank.
int wallpaperIndex();
void setWallpaperIndex(int idx);
}  // namespace settings
