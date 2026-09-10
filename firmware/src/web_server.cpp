#include "web_server.h"
#include "config.h"
#include "library.h"
#include "wifi_setup.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

namespace {
AsyncWebServer* server = nullptr;
std::function<void()> onLibraryChanged;
std::function<void()> onWallpapersChanged;
bool routesBound = false;

struct UploadState {
  File file;
  String path;
  bool ok = true;
};

String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}

String basenameOf(String name) {
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  return name;
}

String sanitizeFileName(String name, const char* fallbackExt) {
  name.replace("\\", "_");
  name.replace("/", "_");
  int dot = name.lastIndexOf('.');
  if (dot > 0) name = name.substring(0, dot);
  String out;
  for (size_t i = 0; i < name.length() && out.length() < 40; i++) {
    char c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '-') {
      out += c;
    } else {
      out += '_';
    }
  }
  if (!out.length()) out = "file";
  if (fallbackExt) out += fallbackExt;
  return out;
}

void listDirJson(AsyncWebServerRequest* request, const char* dirPath, const char* suffix,
                 size_t suffixLen, size_t requiredSize) {
  String json = "[";
  File dir = LittleFS.open(dirPath);
  bool first = true;
  if (dir) {
    File f = dir.openNextFile();
    while (f) {
      String name = basenameOf(f.name());
      f.close();
      bool match = name.length() > suffixLen && name.endsWith(suffix);
      if (match) {
        File body = LittleFS.open(String(dirPath) + "/" + name, "r");
        size_t sz = body ? body.size() : 0;
        if (body) body.close();
        if (!requiredSize || sz == requiredSize) {
          if (!first) json += ",";
          first = false;
          json += "{\"name\":\"" + jsonEscape(name) + "\",\"size\":" + String(sz) + "}";
        }
      }
      f = dir.openNextFile();
    }
  }
  json += "]";
  request->send(200, "application/json", json);
}

void bindBodyUpload(const char* path, const char* dir, const char* requiredExt,
                    size_t requiredSize, std::function<void()>* changed) {
  server->on(
      path, HTTP_POST,
      [changed, requiredSize](AsyncWebServerRequest* request) {
        UploadState* st = reinterpret_cast<UploadState*>(request->_tempObject);
        bool ok = st && st->ok;
        if (ok && requiredSize) {
          File f = LittleFS.open(st->path, "r");
          ok = f && f.size() == requiredSize;
          if (f) f.close();
          if (!ok && st) LittleFS.remove(st->path);
        }
        if (st) {
          delete st;
          request->_tempObject = nullptr;
        }
        if (!ok) {
          request->send(400, "text/plain", "bad upload");
          return;
        }
        request->send(200, "text/plain", "OK");
        if (changed && *changed) (*changed)();
      },
      nullptr,
      [dir, requiredExt](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                         size_t index, size_t total) {
        UploadState* st = reinterpret_cast<UploadState*>(request->_tempObject);
        if (index == 0) {
          String name = request->hasParam("name") ? request->getParam("name")->value()
                                                    : String("file") + requiredExt;
          name = sanitizeFileName(name, requiredExt);
          String full = String(dir) + "/" + name;
          st = new UploadState{LittleFS.open(full, "w"), full, true};
          request->_tempObject = st;
          request->onDisconnect([request]() {
            UploadState* leftover = reinterpret_cast<UploadState*>(request->_tempObject);
            if (!leftover) return;
            leftover->file.close();
            LittleFS.remove(leftover->path);
            delete leftover;
            request->_tempObject = nullptr;
          });
        }
        if (st && st->file) st->file.write(data, len);
        if (index + len >= total && st) st->file.close();
      });
}

void bindRoutes() {
  if (!server || routesBound) return;
  routesBound = true;

  server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server->on("/books", HTTP_GET, [](AsyncWebServerRequest* request) {
    listDirJson(request, BOOKS_DIR, ".cebk", 5, 0);
  });

  server->on("/wallpapers", HTTP_GET, [](AsyncWebServerRequest* request) {
    listDirJson(request, WALLPAPER_DIR, ".bin", 4, WALLPAPER_BYTES);
  });

  bindBodyUpload("/upload", BOOKS_DIR, ".cebk", 0, &onLibraryChanged);
  bindBodyUpload("/wallpapers/upload", WALLPAPER_DIR, ".bin", WALLPAPER_BYTES, &onWallpapersChanged);

  server->on("/notes", HTTP_POST, [](AsyncWebServerRequest* request) {
    String title, author, body;
    if (request->hasParam("title", true)) title = request->getParam("title", true)->value();
    if (request->hasParam("author", true)) author = request->getParam("author", true)->value();
    if (request->hasParam("body", true)) body = request->getParam("body", true)->value();
    if (!body.length() && !title.length()) {
      request->send(400, "text/plain", "need title or text");
      return;
    }
    if (!library::addPlainNote(title, author, body)) {
      request->send(500, "text/plain", "could not save");
      return;
    }
    request->send(200, "text/plain", "OK");
    if (onLibraryChanged) onLibraryChanged();
  });

  server->on("/books/delete", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("name", true)) {
      request->send(400, "text/plain", "missing name");
      return;
    }
    String path = String(BOOKS_DIR) + "/" + basenameOf(request->getParam("name", true)->value());
    bool removed = LittleFS.remove(path);
    request->send(removed ? 200 : 404, "text/plain", "");
    if (removed && onLibraryChanged) onLibraryChanged();
  });

  server->on("/wallpapers/delete", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("name", true)) {
      request->send(400, "text/plain", "missing name");
      return;
    }
    String path = String(WALLPAPER_DIR) + "/" + basenameOf(request->getParam("name", true)->value());
    bool removed = LittleFS.remove(path);
    request->send(removed ? 200 : 404, "text/plain", "");
    if (removed && onWallpapersChanged) onWallpapersChanged();
  });

  // ---------------------------------------------------------------- Wi-Fi join / captive portal
  //
  // Reachable any time (not just under the AP) — joining a home network from the AP's
  // uploader page and checking status from a normal STA session both use these.
  server->on("/wifi/join", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("ssid", true)) {
      request->send(400, "text/plain", "missing ssid");
      return;
    }
    String ssid = request->getParam("ssid", true)->value();
    String pass = request->hasParam("password", true) ? request->getParam("password", true)->value() : String();
    wifi_setup::joinNetwork(ssid, pass);
    // Join is async (Wi-Fi association takes a beat); the caller polls /wifi/status.
    // This response itself may never make it back once the AP's DNS/AP drop mid-flight —
    // that's fine, the phone falls back to its own connectivity check on the new network.
    request->send(200, "text/plain", "Joining " + ssid + "...");
  });

  server->on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    bool connected = WiFi.status() == WL_CONNECTED && !wifi_setup::apOpen();
    String json = String("{\"connected\":") + (connected ? "true" : "false") +
                  ",\"ip\":\"" + (connected ? WiFi.localIP().toString() : String()) + "\"}";
    request->send(200, "application/json", json);
  });

  // OS connectivity-probe paths, answered with a 302 to "/" so the phone's "Sign in to
  // this network" prompt opens straight into the uploader — a 204 (their normal "yes,
  // you have internet" answer) would tell the OS there's nothing to sign into. Only meant
  // to fire while our AP is the one being joined; harmless but pointless in STA mode, so
  // it's skipped there rather than redirecting a genuine internet-connectivity check.
  auto captiveRedirect = [](AsyncWebServerRequest* request) {
    if (!wifi_setup::apOpen()) {
      request->send(404, "text/plain", "not found");
      return;
    }
    request->redirect("/");
  };
  server->on("/generate_204", HTTP_GET, captiveRedirect);         // Android
  server->on("/gen_204", HTTP_GET, captiveRedirect);              // Android (older)
  server->on("/hotspot-detect.html", HTTP_GET, captiveRedirect);  // iOS/macOS
  server->on("/library/test/success.html", HTTP_GET, captiveRedirect);  // iOS/macOS
  server->on("/connecttest.txt", HTTP_GET, captiveRedirect);      // Windows
  server->on("/ncsi.txt", HTTP_GET, captiveRedirect);             // Windows

  // Catch-all for anything else an OS's captive-portal detector probes for that isn't in
  // the specific list above. serveStatic("/") above only claims a path when a matching
  // file actually exists in LittleFS, so this still fires for everything else.
  server->onNotFound([](AsyncWebServerRequest* request) {
    if (wifi_setup::apOpen()) {
      request->redirect("/");
      return;
    }
    request->send(404, "text/plain", "not found");
  });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
}
}  // namespace

void web_server::begin(std::function<void()> libCb, std::function<void()> wpCb) {
  onLibraryChanged = libCb;
  onWallpapersChanged = wpCb;
  if (!LittleFS.exists(BOOKS_DIR)) LittleFS.mkdir(BOOKS_DIR);
  if (!LittleFS.exists(WALLPAPER_DIR)) LittleFS.mkdir(WALLPAPER_DIR);
}

void web_server::start() {
  if (server) return;
  server = new AsyncWebServer(80);
  routesBound = false;
  bindRoutes();
  server->begin();
}

void web_server::stop() {
  if (!server) return;
  server->end();
  delete server;
  server = nullptr;
  routesBound = false;
}
