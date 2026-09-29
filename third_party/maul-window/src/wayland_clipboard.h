// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland clipboard: the seat's data device and its selection. A
// write sets a data source that offers the context's text as UTF-8,
// quoting the serial of the latest input event, and serves each reader
// through the pipe it sends, a piece at each pump. A read takes the
// selection's text through a pipe, a piece at each pump, and answers
// the read requests of every window when the writer closes it, or fails
// them after a deadline; while the program owns the selection, a read
// answers from its own text at once. Only a client with keyboard focus
// hears of the selection.

#ifndef MAUL_WINDOW_SRC_WAYLAND_CLIPBOARD_H
#define MAUL_WINDOW_SRC_WAYLAND_CLIPBOARD_H

#include "wayland.h"

// Binds the data device manager.
void mwinWaylandBindDataManager(mwinWaylandPlatform* platform, uint32_t name, uint32_t version);

// Marks the clipboard's pipes closed; the backend's start calls it
// before anything can fail.
void mwinWaylandInitClipboard(mwinWaylandClipboard* clipboard);

// Makes the seat's data device once both are bound, and ends it.
void mwinWaylandAttachClipboard(mwinWaylandPlatform* platform);
void mwinWaylandDetachClipboard(mwinWaylandPlatform* platform);

// Carries out a clipboard request: its outcome, or -1 while the read
// goes on.
int mwinWaylandWriteClipboard(mwinWaylandPlatform* platform);
int mwinWaylandReadClipboard(mwinWaylandPlatform* platform);

// Moves the pipes on, without waiting.
void mwinWaylandPumpClipboard(mwinWaylandPlatform* platform, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_WAYLAND_CLIPBOARD_H
