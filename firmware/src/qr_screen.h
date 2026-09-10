#pragma once
// Full-screen QR code, drawn through Renderer::renderCustom (same Adafruit_GFX pixel path
// the games use). Used for both the AP-join QR (Wi-Fi screen / Home menu) and the
// STA-mode "open this URL" QR.

#include <Arduino.h>

class Renderer;

namespace qr_screen {

// Draws `payload` as a QR code plus a text caption (SSID, URL, ...), always as a full
// refresh — partial-refresh ghosting is exactly the kind of noise a camera's QR decoder
// gives up on. Renders a caption-only screen (no QR box) if `payload` doesn't fit, which
// shouldn't happen for anything this device ever encodes (see qr_screen.cpp).
void show(Renderer& renderer, const String& payload, const String& caption);

}  // namespace qr_screen
