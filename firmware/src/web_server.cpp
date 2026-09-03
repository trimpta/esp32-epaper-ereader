#include "web_server.h"
#include "config.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

namespace {
AsyncWebServer server(80);

// Per-upload state, stashed on the request itself (AsyncWebServerRequest::_tempObject)
// rather than a file-scope File. A single shared File meant two overlapping uploads (e.g.
// a retry from a flaky connection) interleaved writes into the same handle and corrupted
// both .cebk files; it also leaked the handle forever if a client disconnected mid-upload
// instead of ever reaching index+len >= total.
struct UploadState {
  File file;
  String path;
};
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
        String name = f.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        // Skip stray/partial files (e.g. a .tmp left by an aborted upload) so the loader
        // page's delete list matches what library:: actually treats as a book.
        if (name.length() > 5 && name.endsWith(".cebk")) {
          if (!first) json += ",";
          first = false;
          json += "{\"name\":\"" + name + "\",\"size\":" + String(f.size()) + "}";
        }
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
        UploadState* st = reinterpret_cast<UploadState*>(request->_tempObject);
        if (index == 0) {
          String name = request->hasParam("name") ? request->getParam("name")->value()
                                                    : String("book.cebk");
          name.replace("/", "_");
          if (!name.endsWith(".cebk")) name += ".cebk";
          String path = String(BOOKS_DIR) + "/" + name;
          st = new UploadState{LittleFS.open(path, "w"), path};
          request->_tempObject = st;
          // A client that disconnects mid-transfer never reaches index+len >= total below,
          // so the file would otherwise stay open (and half-written on flash) forever.
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
        if (index + len >= total && st) {
          st->file.close();
          delete st;
          request->_tempObject = nullptr;
        }
      });

  server.on("/books/delete", HTTP_POST, [onLibraryChanged](AsyncWebServerRequest* request) {
    if (!request->hasParam("name", true)) {
      request->send(400, "text/plain", "missing name");
      return;
    }
    String path = String(BOOKS_DIR) + "/" + request->getParam("name", true)->value();
    bool removed = LittleFS.remove(path);
    request->send(removed ? 200 : 404, "text/plain", "");
    // Without this, library::'s in-RAM list (and activeBook/browseBook if the deleted
    // book was open) stayed stale until the next upload or reboot.
    if (removed && onLibraryChanged) onLibraryChanged();
  });

  // Lets the standalone converter/index.html (opened from disk, or hosted elsewhere)
  // upload directly to the device, not just the copy the device itself serves.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
}
