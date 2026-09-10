#include "qr_screen.h"
#include "config.h"
#include "renderer.h"

// esp_qrcode_* — bundled with the ESP32 Arduino core (its espressif__qrcode component),
// not a separate Arduino library. Deliberately NOT ricmoo/QRCode: that library's header is
// also named qrcode.h, and the core's own copy (same filename, on the compiler's default
// include path) wins the search — `#include <qrcode.h>` here always resolves to this one,
// so pulling in ricmoo's as a lib_dep would just be dead weight that never actually compiles
// against. Bonus: one fewer dependency, since this ships with the toolchain already.
#include <qrcode.h>
#include <algorithm>

namespace {

// Modules of blank border on every side — the "quiet zone" scanners rely on to find the
// code's edges at all. The spec calls for 4; this device has so little screen that 2 is
// what actually fits without shrinking the modules into unreadable pixels, and 2 is what
// most phone cameras handle fine in practice.
const uint8_t QUIET_ZONE_MODULES = 2;

// esp_qrcode_generate()'s display_func is a plain C function pointer — no captures — so
// the drawing context it needs for the one call it makes lives here instead. Set just
// before the call and cleared just after; nothing else in this file runs concurrently
// with it (it's a synchronous call on the render thread, not an async callback).
Adafruit_GFX* gGfx = nullptr;
U8G2_FOR_ADAFRUIT_GFX* gU8f = nullptr;
String gCaption;

// Splits `text` on '\n' and draws each line, top-down, starting at (x, firstBaselineY).
void drawCaption(U8G2_FOR_ADAFRUIT_GFX& u8f, const String& text, int x, int firstBaselineY,
                  int lineHeight) {
  int start = 0;
  int y = firstBaselineY;
  while (start <= (int)text.length()) {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    u8f.setCursor(x, y);
    u8f.print(line);
    y += lineHeight;
    if (nl < 0) break;
    start = nl + 1;
  }
}

void onQrReady(esp_qrcode_handle_t qr) {
  if (!gGfx || !gU8f) return;

  int size = esp_qrcode_get_size(qr);
  int board = size + 2 * QUIET_ZONE_MODULES;
  int availHeight = PAGE_HEIGHT_PX - 2 * MARGIN_PX;
  // Reserve the right ~45% of the panel for caption text — the QR only needs to be
  // legible, not large, and the caption (SSID/URL, plus a password fallback for a camera
  // that won't scan) needs real room next to it.
  int qrAreaWidth = (PAGE_WIDTH_PX * 55) / 100;
  int scale = std::min(availHeight / board, qrAreaWidth / board);
  if (scale < 1) scale = 1;
  int qrPx = board * scale;

  int qrX = MARGIN_PX;
  int qrY = (PAGE_HEIGHT_PX - qrPx) / 2;

  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (!esp_qrcode_get_module(qr, x, y)) continue;
      gGfx->fillRect(qrX + (x + QUIET_ZONE_MODULES) * scale, qrY + (y + QUIET_ZONE_MODULES) * scale,
                     scale, scale, INK);
    }
  }

  drawCaption(*gU8f, gCaption, qrX + qrPx + MARGIN_PX, MARGIN_PX + 8, LINE_HEIGHT_PX);
}

}  // namespace

void qr_screen::show(Renderer& renderer, const String& payload, const String& caption) {
  renderer.forceFullRefreshNext();  // partial-refresh ghosting is exactly what breaks a scan
  gCaption = caption;

  renderer.renderCustom([&](Adafruit_GFX& g, U8G2_FOR_ADAFRUIT_GFX& u8f) {
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setForegroundColor(INK);
    gGfx = &g;
    gU8f = &u8f;

    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = onQrReady;
    // Generous ceiling: an SSID+password or a LAN URL is well under what version 10
    // holds even at the lowest ECC level, so this only ever costs unused headroom.
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    if (esp_qrcode_generate(&cfg, payload.c_str()) != ESP_OK) {
      // Shouldn't happen for anything this device actually encodes — fall back to
      // text-only rather than drawing nothing.
      drawCaption(u8f, caption, MARGIN_PX, MARGIN_PX + 8, LINE_HEIGHT_PX);
    }

    gGfx = nullptr;
    gU8f = nullptr;
  });
}
