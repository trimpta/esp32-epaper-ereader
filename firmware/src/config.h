#pragma once

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

// Driver chip is SSD1680Z or JD79661 depending on panel batch (Elecrow dual-sources); the
// schematic's U3 pinout (RES/BUSY/D-C/CS/SCL/SDA + VSH/VSL/VGH/VGL/VCOM analog rails) matches
// the SSD1680 family either way, so GxEPD2's SSD1680 class (renderer.cpp) should cover both —
// unconfirmed until first boot.

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
static const int FULL_REFRESH_EVERY_N_PAGES = 8;

static const char* BOOKS_DIR = "/books";
static const char* WIFI_AP_NAME = "CrowPanel-Reader-Setup";

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
