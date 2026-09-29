// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// text/uri-list (RFC 2483), as Wayland and X11 drops carry files: one
// URI a line, lines ended by CRLF or LF, lines beginning with # left
// out. A file URI (file:///path, or file://localhost/path) gives its
// path, percent-decoded; a URI of another scheme or host names no file
// the program can open and is left out, the drop marked truncated.

#ifndef MAUL_WINDOW_SRC_URI_LIST_H
#define MAUL_WINDOW_SRC_URI_LIST_H

#include "core.h"

// Adds the paths of a list to the context's drop; the list is decoded
// in place.
void mwinGatherUriList(mwinContext* context, char* list, size_t length);

// The path of one file URI, decoded in place: its length, with
// *pathOut where it starts, or 0 for a URI that names no local file.
size_t mwinFileUriPath(char* uri, size_t length, char** pathOut);

#endif // MAUL_WINDOW_SRC_URI_LIST_H
