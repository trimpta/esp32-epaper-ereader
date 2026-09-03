#pragma once
// Serves firmware/data/index.html (the tiny loader page — see its header comment for
// why it's tiny) and the book upload/list/delete API.

#include <functional>

namespace web_server {

// onLibraryChanged fires after a book finishes uploading or is deleted over the web API.
// main.cpp wires it to ui::onLibraryChanged(), which rescans the library and, if the
// Library screen is on-screen, redraws it.
void begin(std::function<void()> onLibraryChanged);

}  // namespace web_server
