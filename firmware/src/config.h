#pragma once

#include <cstddef>  // size_t, for WALLPAPER_BYTES below

// Button/rotary GPIOs — from Elecrow's published pin table for the 2.13" HMI display.
// https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_2.13-inch_Arduino_Tutorial.html
static const int PIN_MENU_BUTTON = 2;
static const int PIN_EXIT_BUTTON = 1;
static const int PIN_ROTARY_DOWN = 4;
static const int PIN_ROTARY_UP = 6;
static const int PIN_ROTARY_CONF = 5;

// Display SPI pins — read off Elecrow's schematic (CrowPanel_ESP32_Display-2.13(E)_Inch.pdf,
// "EPD interface" block, matching the IO#_NAME net labels there — same naming convention as
// the button pins above, which that schematic independently confirms). Not yet continuity-
// tested against a physical board, so still worth a quick multimeter sanity check before
// relying on it, but this is a real schematic read, not a guess.
static const int PIN_EPD_MOSI = 11;  // IO11_SPI_MOSI -> SDA
static const int PIN_EPD_SCK = 12;   // IO12_SPI_CLK  -> SCL
static const int PIN_EPD_CS = 14;    // IO14_CS
static const int PIN_EPD_DC = 13;    // IO13_D/C
static const int PIN_EPD_RST = 10;   // IO10_RES
static const int PIN_EPD_BUSY = 9;   // IO9_BUSY

// The panel's 3.3V rail is separately gated (schematic: "IO7_LCD_3.3_CTL"), not just tied
// to always-on 3.3V — renderer.cpp must drive this high before display.init() or nothing
// will respond on the SPI lines above.
static const int PIN_EPD_POWER_CTL = 7;

// Driver chip: confirmed JD79661 on real hardware (2026-09-04) — GxEPD2's generic SSD1680
// class (GxEPD2_213_BN) hit a busy-wait timeout and never actually refreshed the panel.
// renderer.cpp uses GxEPD2_213c_GDEY0213F51 instead, GxEPD2's JD79661-specific driver.

// Power/status LED — schematic "IO19_LED", other terminal to ground, so this pin sources
// current through it: drive HIGH to light it, LOW to turn it off.
static const int PIN_STATUS_LED = 19;

// Panel geometry. Native resolution is 122x250 portrait, but that's only ~19
// characters/line — tedious to read. Rendered landscape instead (setRotation(1) in
// renderer.cpp transposes the draw surface; the physical panel and buttons don't move)
// for a ~40 char/line, ~10 line/page layout much closer to normal reading rhythm.
// Confirmed against simulator/index.html before committing to it here.
static const int PAGE_WIDTH_PX = 250;
static const int PAGE_HEIGHT_PX = 122;
static const int MARGIN_PX = 4;
static const int LINE_HEIGHT_PX = 11;
static const int HEADING_LINE_HEIGHT_PX = 14;

// Full refresh every N partial-refresh page turns, to clear e-ink ghosting.
// Tied to page turns (a counter), not a timer — see docs/ARCHITECTURE.md.
// Runtime-adjustable from the on-device Settings screen (unlike the layout constants
// above, which are baked into every .cebk at conversion time) — this default is just
// the starting value; settings::refreshEveryNPages() is what the renderer reads.
static const int FULL_REFRESH_EVERY_N_PAGES = 8;

// Menu/list screens. Mirrors simulator/index.html's LIST_TOP_MARGIN / LIST_LINE_H /
// LIST_FONT_PX so the on-device menus lay out the same way the simulator previews them.
static const int LIST_TOP_MARGIN_PX = 4;
static const int LIST_LINE_HEIGHT_PX = 12;
static const int LIST_FIRST_BASELINE_PX = LIST_TOP_MARGIN_PX + 8;

// Input gesture timing — matches bindHoldGesture()'s holdMs default in the simulator.
static const unsigned long HOLD_GESTURE_MS = 550;

// Idle sleep. 5 minutes while reading a page, 1 minute anywhere else — a menu sitting
// open is a much stronger idle signal than a page you may still be reading.
static const unsigned long IDLE_SLEEP_READING_MS = 5UL * 60UL * 1000UL;
static const unsigned long IDLE_SLEEP_MENU_MS = 60UL * 1000UL;

static const char* BOOKS_DIR = "/books";
static const char* WALLPAPER_DIR = "/wallpapers";
static const char* STATE_FILE = "/state.json";
static const char* WIFI_AP_NAME = "CrowPanel-Reader-Setup";

// A wallpaper is a raw 1-bit bitmap at the panel's native resolution, nothing else —
// see docs/FLASH_BUDGET.md "Wallpapers: cheap, by design".
static const size_t WALLPAPER_BYTES = (122 * 250) / 8;  // 3,813 B

// Battery percentage — NOT available out of the box. The schematic's charger circuit
// (DFN8_4054A linear charger + a directly-wired LED) has no net name anywhere in the
// full IO list for battery voltage or charge status, so the MCU has zero visibility into
// battery state as shipped — this isn't a missing pin definition, it's a missing sense
// line. To enable it: add a resistor divider from BAT+ to a free ADC1-capable GPIO (ADC2
// doesn't work while WiFi is active) — two 100k resistors gives a 2x divider, keeping a
// 4.2V-max cell under the ADC's ~3.3V ceiling — then set PIN_BATTERY_ADC below. See
// battery.cpp. Doesn't touch the display panel, just two SMD resistors on the PCB.
static const int PIN_BATTERY_ADC = -1;   // -1 = disabled (battery::readPercent() returns -1)
static const float BATTERY_DIVIDER_RATIO = 2.0f;  // (R1+R2)/R2
