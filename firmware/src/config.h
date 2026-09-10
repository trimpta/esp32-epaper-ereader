#pragma once

#include <cstddef>  // size_t, for WALLPAPER_BYTES below

// Button/rotary GPIOs — from Elecrow's published pin table for the 2.13" HMI display.
// https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_2.13-inch_Arduino_Tutorial.html
static const int PIN_MENU_BUTTON = 2;
static const int PIN_EXIT_BUTTON = 1;
static const int PIN_ROTARY_DOWN = 4;
static const int PIN_ROTARY_UP = 6;
static const int PIN_ROTARY_CONF = 5;

// Display SPI pins — Elecrow schematic + their working arduino-v1.2 example (same numbers).
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

// Panel controller speaks Elecrow's UC81xx command set (0x00 init, 0x13 RAM, 0x17 refresh),
// not SSD1680. See firmware/src/epd_panel.cpp — GxEPD2_213_BN left the screen blank.

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
// Body text is helvR08 (see renderer.cpp fontForFlags). Both heights are baked into every
// .cebk at conversion time, so a change here means books converted against the old numbers
// paginate wrong: bump converter/converter.js's LAYOUT to match and re-convert.
static const int LINE_HEIGHT_PX = 11;
static const int HEADING_LINE_HEIGHT_PX = 14;

// No full-refresh cadence setting here anymore — every frame is a full refresh
// unconditionally (renderer.cpp's beginFrame(), GitHub issue #12). The periodic-cadence
// idea only mattered for a partial-refresh ghosting cost the simulator modeled but real
// hardware doesn't pay, so there was nothing left to tune.

// Menu/list screens. Mirrors simulator/index.html's LIST_TOP_MARGIN / LIST_LINE_H /
// LIST_FONT_PX so the on-device menus lay out the same way the simulator previews them.
static const int LIST_TOP_MARGIN_PX = 4;
static const int LIST_LINE_HEIGHT_PX = 12;
static const int LIST_FIRST_BASELINE_PX = LIST_TOP_MARGIN_PX + 8;

// Input gesture timing — matches bindHoldGesture()'s holdMs default in the simulator.
static const unsigned long HOLD_GESTURE_MS = 550;

// Idle behavior, in two independent stages — see docs/ARCHITECTURE.md "Power".
//
// Stage 1, light sleep: after a short quiet gap the CPU light-sleeps with the panel image
// left standing, waking on any key. The key that wakes it is *acted on*, so this is
// invisible to the reader — a page turn still costs one press — which is why the gap can
// be this short. Reading gets a slightly longer gap than menus only because a page turn
// is often followed by a second one (a skim), while a menu press is usually the last of
// its burst.
//
// Stage 2, sleep screen: only after this long with no key at all does the wallpaper go up
// and the panel power down. That's the visible, "I put it away" state, so it's deliberately
// far out — the old 5min/1min limits fired mid-chapter while the reader was still reading.
static const unsigned long IDLE_LIGHT_SLEEP_READING_MS = 1500;
static const unsigned long IDLE_LIGHT_SLEEP_MENU_MS = 500;
static const unsigned long IDLE_SLEEP_SCREEN_MS = 10UL * 60UL * 1000UL;

static const char* BOOKS_DIR = "/books";
static const char* WALLPAPER_DIR = "/wallpapers";
static const char* STATE_FILE = "/state.json";
static const char* WIFI_AP_NAME = "CrowPanel-Reader-Setup";
// The AP used to be a one-time provisioning fallback (open, since it only existed for the
// few minutes of first setup). It's now the normal book-transfer path — reachable from the
// Home menu any time — so it gets a real password: min 8 chars for WPA2-PSK. Fixed and
// public in source rather than randomly generated, because the QR code is what actually
// carries it to the phone; a person is never expected to type this.
static const char* WIFI_AP_PASSWORD = "readerbooks";
// softAP()'s default gateway/IP (no explicit softAPConfig call needed) — captive-portal
// DNS answers every query with this address.
static const char* WIFI_AP_IP = "192.168.4.1";

// A wallpaper is a raw copy of the panel framebuffer (250×122 landscape, 16 bytes per
// column = 4000 B) — same bit layout as EpdGfx::drawPixel, 1=paper 0=ink.
static const size_t WALLPAPER_BYTES = 16 * 250;

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
