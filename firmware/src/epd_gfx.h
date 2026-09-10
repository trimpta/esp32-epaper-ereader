#pragma once
// Adafruit_GFX surface over Elecrow's UC81xx driver. The canvas is already landscape
// 250x122 — the same coordinates renderer.cpp uses (PAGE_WIDTH_PX x PAGE_HEIGHT_PX).
// Pixel addressing copies Elecrow's USE_HORIZONTIAL==2 DrawPoint.

#include "epd_panel.h"
#include <Adafruit_GFX.h>

class EpdGfx : public Adafruit_GFX {
 public:
  EpdGfx() : Adafruit_GFX(EPD_BUF_W, EPD_NATIVE_W) {}  // 250 x 122

  void begin();
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
  void copyBuffer(const uint8_t* bits);  // native 4000-byte panel buffer (same layout as drawPixel)

  void setFullWindow() { fullRefresh_ = true; }
  void setPartialWindow(int16_t, int16_t, int16_t, int16_t) { fullRefresh_ = false; }

  void firstPage();
  bool nextPage();
  void hibernate();
  void wake();  // EPD_Init after hibernate — panel will not refresh until this

 private:
  void flush();

  uint8_t buffer_[EPD_BUF_BYTES];
  uint8_t prev_[EPD_BUF_BYTES];  // last flushed frame, written to 0x10 on DU
  bool fullRefresh_ = true;
  bool pageOpen_ = false;
};
