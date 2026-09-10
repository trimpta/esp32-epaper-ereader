#pragma once
// Bit-bang SPI + control pins for the CrowPanel 2.13" panel.
// Ported from Elecrow's working arduino-v1.2 `spi.h` / `spi.cpp` (renamed so it cannot
// collide with Arduino's <SPI.h> on Windows). Do not switch this to hardware SPI without
// re-checking on device — the OEM demo that actually drives this panel is bit-bang.

#include "config.h"
#include <Arduino.h>

#define EPD_SCK_Clr() digitalWrite(PIN_EPD_SCK, LOW)
#define EPD_SCK_Set() digitalWrite(PIN_EPD_SCK, HIGH)
#define EPD_MOSI_Clr() digitalWrite(PIN_EPD_MOSI, LOW)
#define EPD_MOSI_Set() digitalWrite(PIN_EPD_MOSI, HIGH)
#define EPD_RES_Clr() digitalWrite(PIN_EPD_RST, LOW)
#define EPD_RES_Set() digitalWrite(PIN_EPD_RST, HIGH)
#define EPD_DC_Clr() digitalWrite(PIN_EPD_DC, LOW)
#define EPD_DC_Set() digitalWrite(PIN_EPD_DC, HIGH)
#define EPD_CS_Clr() digitalWrite(PIN_EPD_CS, LOW)
#define EPD_CS_Set() digitalWrite(PIN_EPD_CS, HIGH)
#define EPD_ReadBUSY digitalRead(PIN_EPD_BUSY)

void EPD_GPIOInit(void);
void EPD_WR_Bus(uint8_t dat);
void EPD_WR_REG(uint8_t reg);
void EPD_WR_DATA8(uint8_t dat);
