#include "settings.h"
#include "config.h"

#include <Preferences.h>
#include <algorithm>

namespace {
Preferences prefs;
bool ready = false;
int wallpaper = 0;
}  // namespace

void settings::begin() {
  ready = prefs.begin("reader", false);
  if (!ready) return;
  wallpaper = prefs.getInt("wallpaper", 0);
}

int settings::wallpaperIndex() {
  return wallpaper;
}

void settings::setWallpaperIndex(int idx) {
  if (idx == wallpaper) return;
  wallpaper = idx;
  if (ready) prefs.putInt("wallpaper", idx);
}
