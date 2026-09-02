#pragma once
// Serves firmware/data/index.html (the tiny loader page — see its header comment for
// why it's tiny) and the book upload/list/delete API.

#include <functional>

namespace web_server {

// onLibraryChanged fires after a book finishes uploading. main.cpp doesn't yet do
// anything with it beyond the TODO noted there — no book-picker UI in this skeleton.
void begin(std::function<void()> onLibraryChanged);

}  // namespace web_server
