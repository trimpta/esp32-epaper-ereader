#include "web_server.h"
#include "config.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

namespace {
AsyncWebServer server(80);
File uploadFile;
}  // namespace

void web_server::begin(std::function<void()> onLibraryChanged) {
  if (!LittleFS.exists(BOOKS_DIR)) LittleFS.mkdir(BOOKS_DIR);

  // Loader page (firmware/data/index.html) + any other static assets in LittleFS.
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/books", HTTP_GET, [](AsyncWebServerRequest* request) {
    String json = "[";
    File dir = LittleFS.open(BOOKS_DIR);
    bool first = true;
    if (dir) {
      File f = dir.openNextFile();
      while (f) {
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"" + String(f.name()) + "\",\"size\":" + String(f.size()) + "}";
        f = dir.openNextFile();
      }
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  server.on(
      "/upload", HTTP_POST,
      [onLibraryChanged](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "OK");
        if (onLibraryChanged) onLibraryChanged();
      },
      nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          String name = request->hasParam("name") ? request->getParam("name")->value()
                                                    : String("book.cebk");
          name.replace("/", "_");
          if (!name.endsWith(".cebk")) name += ".cebk";
          uploadFile = LittleFS.open(String(BOOKS_DIR) + "/" + name, "w");
        }
        if (uploadFile) uploadFile.write(data, len);
        if (index + len >= total && uploadFile) uploadFile.close();
      });

  server.on("/books/delete", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("name", true)) {
      request->send(400, "text/plain", "missing name");
      return;
    }
    String path = String(BOOKS_DIR) + "/" + request->getParam("name", true)->value();
    request->send(LittleFS.remove(path) ? 200 : 404, "text/plain", "");
  });

  // Lets the standalone converter/index.html (opened from disk, or hosted elsewhere)
  // upload directly to the device, not just the copy the device itself serves.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
}
