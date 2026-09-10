// Run this once on the actual ESP32-S3 (or any ESP32/AVR — it never touches the
// display hardware, only U8g2's font tables) to get real glyph widths for the
// exact fonts firmware/src/renderer.h uses. Paste the JSON it prints over Serial
// into converter/font-metrics.json and point converter/index.html /
// firmware/data/index.html at that file instead of font-metrics.example.json.
//
// Why this exists: docs/FORMAT.md's whole point is that pagination computed in
// the browser must match what the device renders pixel-for-pixel. The
// placeholder widths in font-metrics.example.json are guesses; this sketch
// removes the guessing.
//
// Requires: U8g2 library (https://github.com/olikraus/U8g2_Arduino)
// Keep FONT_* names here in sync with firmware/src/renderer.h.

#include <U8g2lib.h>

// No display attached — we never call begin()/sendBuffer(), only the font
// metric functions, so any constructor works as a stand-in for font access.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

struct FontEntry {
  const char* key;      // must match converter's fontMetrics key names
  const uint8_t* font;
};

// TODO(verify): true italic isn't available in u8g2's stock set at this size —
// "italic" reuses the regular face here. See docs/ARCHITECTURE.md.
FontEntry FONTS[] = {
  { "regular",  u8g2_font_helvR08_tf },
  { "bold",     u8g2_font_helvB08_tf },
  { "italic",   u8g2_font_helvR08_tf },
  { "heading1", u8g2_font_helvB12_tf },
  { "heading2", u8g2_font_helvB10_tf },
};

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(1000);

  Serial.println("{");
  for (size_t f = 0; f < sizeof(FONTS) / sizeof(FONTS[0]); f++) {
    u8g2.setFont(FONTS[f].font);
    Serial.printf("  \"%s\": { \"default\": %d, \"widths\": {\n",
                   FONTS[f].key, u8g2.getMaxCharWidth());

    for (int c = 0x20; c <= 0x7E; c++) {
      char buf[2] = { (char)c, 0 };
      int w = u8g2.getUTF8Width(buf);
      // JSON-escape the two characters that matter in this ASCII range
      char key[3];
      if (c == '"') { key[0] = '\\'; key[1] = '"'; key[2] = 0; }
      else if (c == '\\') { key[0] = '\\'; key[1] = '\\'; key[2] = 0; }
      else { key[0] = (char)c; key[1] = 0; }

      Serial.printf("    \"%s\": %d%s\n", key, w,
                     (c == 0x7E) ? "" : ",");
    }
    Serial.printf("  }}%s\n",
                   (f == sizeof(FONTS) / sizeof(FONTS[0]) - 1) ? "" : ",");
  }
  Serial.println("}");
  Serial.println("--- copy the JSON above into converter/font-metrics.json ---");
}

void loop() {}
