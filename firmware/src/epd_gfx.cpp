#include "epd_gfx.h"
#include "config.h"
#include <string.h>

void EpdGfx::begin() {
  pinMode(PIN_EPD_POWER_CTL, OUTPUT);
  digitalWrite(PIN_EPD_POWER_CTL, HIGH);
  delay(10);

  EPD_Init();
  memset(buffer_, 0xFF, sizeof(buffer_));
  memset(prev_, 0xFF, sizeof(prev_));
  EPD_Clear();
  EPD_Update(1200);
}

void EpdGfx::drawPixel(int16_t x, int16_t y, uint16_t color) {
  // Landscape: x along 250, y along 122. Elecrow DrawPoint used ypoint = 249-x, which
  // put left-margin text on the right and mirrored glyphs. Device wants ypoint = x.
  if ((x < 0) || (y < 0) || (x >= EPD_BUF_W) || (y >= EPD_NATIVE_W)) return;
  const uint16_t xpoint = (uint16_t)y;
  const uint16_t ypoint = (uint16_t)x;
  const uint32_t addr = (xpoint / 8) + (uint32_t)ypoint * (EPD_BUF_H / 8);
  const uint8_t mask = (uint8_t)(0x80 >> (xpoint % 8));
  // 1 = white, 0 = black. Any non-white GFX color is ink — u8g2's default fg is 1,
  // not 0, so `color == 0` would drop glyphs.
  if (color < 0x8000) {
    buffer_[addr] = (uint8_t)(buffer_[addr] & ~mask);
  } else {
    buffer_[addr] = (uint8_t)(buffer_[addr] | mask);
  }
}

void EpdGfx::fillScreen(uint16_t color) {
  memset(buffer_, color < 0x8000 ? 0x00 : 0xFF, sizeof(buffer_));
}

void EpdGfx::copyBuffer(const uint8_t* bits) {
  memcpy(buffer_, bits, sizeof(buffer_));
}

void EpdGfx::firstPage() { pageOpen_ = true; }

bool EpdGfx::nextPage() {
  if (!pageOpen_) return false;
  pageOpen_ = false;
  flush();
  return false;
}

void EpdGfx::flush() {
  EPD_WR_REG(0x50);
  EPD_WR_DATA8(0xD7);
  // Always reload old RAM. After EPD_Sleep + Init the controller forgets 0x10, but the
  // glass still shows the last image — DU/GC against garbage is the post-wake smear.
  EPD_WR_REG(0x10);
  for (int i = 0; i < EPD_BUF_BYTES; i++) EPD_WR_DATA8(prev_[i]);
  EPD_WR_REG(0x13);
  for (int i = 0; i < EPD_BUF_BYTES; i++) EPD_WR_DATA8(buffer_[i]);
  if (fullRefresh_) {
    lut_GC();
    EPD_Update(1200);
  } else {
    lut_DU();
    EPD_Update(300);
  }
  memcpy(prev_, buffer_, sizeof(prev_));
}

void EpdGfx::hibernate() {
  delay(200);
  EPD_Sleep();
}

void EpdGfx::wake() {
  EPD_Init();
  // prev_ stays the sleep frame — that is still on the glass. Do not fill white.
}
