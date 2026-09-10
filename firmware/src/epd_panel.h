#pragma once
// UC81xx-style init, LUT, RAM write, and refresh for the CrowPanel 2.13" panel.
// Ported from Elecrow arduino-v1.2 `EPD_Init.cpp` — the path the factory demo actually
// runs. SSD1680 leftovers in that file (EPD_HW_Init_Fast / EPD_PartUpdate) are omitted.

#include "epd_bus.h"
#include <stdint.h>

static const int EPD_NATIVE_W = 122;
static const int EPD_NATIVE_H = 250;
// 122 pixels padded to 128 bits per column, times 250 columns.
static const int EPD_BUF_W = 250;
static const int EPD_BUF_H = 128;
static const int EPD_BUF_BYTES = (EPD_BUF_H / 8) * EPD_BUF_W;  // 4000

void EPD_READBUSY(void);
void EPD_HW_SW_RESET(void);
void EPD_Sleep(void);
void EPD_Update(uint32_t settleMs = 1200);
void EPD_Clear(void);
void EPD_DisplayImage(const uint8_t* imageBW);
void EPD_Init(void);
void lut_GC(void);
void lut_DU(void);
