#pragma once
// Serves firmware/data/index.html and the book / wallpaper upload APIs.

#include <functional>

namespace web_server {

// Stores callbacks and ensures /books and /wallpapers exist. Does **not** start TCP —
// constructing AsyncWebServer while WiFi is WIFI_OFF crashes (null FreeRTOS queue).
void begin(std::function<void()> onLibraryChanged, std::function<void()> onWallpapersChanged);

// Bind routes and listen on :80. Call only after STA or the AP is up.
void start();

// Stop listening. Safe if never started.
void stop();

}  // namespace web_server
