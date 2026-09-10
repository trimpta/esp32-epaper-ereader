#include "epd_bus.h"

void EPD_GPIOInit(void) {
  pinMode(PIN_EPD_SCK, OUTPUT);
  pinMode(PIN_EPD_MOSI, OUTPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_BUSY, INPUT);
}

void EPD_WR_Bus(uint8_t dat) {
  EPD_CS_Clr();
  for (uint8_t i = 0; i < 8; i++) {
    EPD_SCK_Clr();
    if (dat & 0x80) {
      EPD_MOSI_Set();
    } else {
      EPD_MOSI_Clr();
    }
    EPD_SCK_Set();
    dat <<= 1;
  }
  EPD_CS_Set();
}

void EPD_WR_REG(uint8_t reg) {
  EPD_DC_Clr();
  EPD_WR_Bus(reg);
  EPD_DC_Set();
}

void EPD_WR_DATA8(uint8_t dat) {
  EPD_DC_Set();
  EPD_WR_Bus(dat);
}
