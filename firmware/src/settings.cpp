#include "settings.h"
#include "config.h"

#include <Preferences.h>
#include <algorithm>

namespace {
Preferences prefs;
bool ready = false;
int refreshEvery = FULL_REFRESH_EVERY_N_PAGES;
int wallpaper = 0;
}  // namespace

void settings::begin() {
  ready = prefs.begin("reader", false);
  if (!ready) return;
  refreshEvery = prefs.getInt("refresh", FULL_REFRESH_EVERY_N_PAGES);
  wallpaper = prefs.getInt("wallpaper", 0);
  refreshEvery = std::max(REFRESH_MIN, std::min(REFRESH_MAX, refreshEvery));
}

int settings::refreshEveryNPages() {
  return refreshEvery;
}

void settings::setRefreshEveryNPages(int n) {
  n = std::max(REFRESH_MIN, std::min(REFRESH_MAX, n));
  if (n == refreshEvery) return;
  refreshEvery = n;
  if (ready) prefs.putInt("refresh", n);
}

int settings::wallpaperIndex() {
  return wallpaper;
}

void settings::setWallpaperIndex(int idx) {
  if (idx == wallpaper) return;
  wallpaper = idx;
  if (ready) prefs.putInt("wallpaper", idx);
}
