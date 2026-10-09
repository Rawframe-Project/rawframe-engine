// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland clipboard and primary selection (mwin-0029). The
// clipboard is the seat's data device and its selection
// (wayland_clipboard.c); the primary selection is its protocol's device
// and selection, text only, where the compositor has it
// (wayland_primary.c). A write sets a source that offers the context's
// text as UTF-8 and, for the clipboard, each MIME type of its data,
// quoting the serial of the latest input event, and serves each reader
// through the pipe it sends, a piece at each pump. A read takes the
// selection's text, or the data of a type its offer has, through a pipe
// a piece at each pump, and answers the reads it answers when the writer
// closes it, or fails them after a deadline; one read runs at a time,
// the others waiting their turn; while the program owns the selection, a
// read answers from its own copy at once (wayland_clipboard_read.c).
// Only a client with keyboard focus hears of the selections.

#ifndef MAUL_WINDOW_SRC_WAYLAND_CLIPBOARD_H
#define MAUL_WINDOW_SRC_WAYLAND_CLIPBOARD_H

#include "wayland.h"

// Binds the data device manager, and the primary selection's.
void mwinWaylandBindDataManager(mwinWaylandPlatform* platform, uint32_t name, uint32_t version);
void mwinWaylandBindPrimaryManager(mwinWaylandPlatform* platform, uint32_t name, uint32_t version);

// Marks the clipboard's pipes closed; the backend's start calls it
// before anything can fail.
void mwinWaylandInitClipboard(mwinWaylandClipboard* clipboard);

// Makes the seat's devices once the seat and their managers are bound,
// and ends them.
void mwinWaylandAttachClipboard(mwinWaylandPlatform* platform);
void mwinWaylandDetachClipboard(mwinWaylandPlatform* platform);

// Carries out a clipboard or primary selection write or read: its
// outcome, or -1 for a read answered later.
int mwinWaylandWriteSelection(mwinWaylandPlatform* platform, mwinRequestKind kind);
int mwinWaylandReadSelection(mwinWaylandPlatform* platform, const mwinRequest* request);

// Moves the pipes on, without waiting.
void mwinWaylandPumpClipboard(mwinWaylandPlatform* platform, uint64_t nowNs);

// Between the files: the index of a text type the backend offers and
// takes, best first, or -1, and the type of an index; offers every text
// type on a source by its offer request; takes a reader's pipe for a
// source, or closes it when every place is taken; closes the readers'
// pipes of a source; the primary selection's device and write
// (wayland_primary.c); and the read's part: failing it when the seat
// goes, and its pipe at each pump (wayland_clipboard_read.c).
int8_t mwinWaylandTextType(const char* type);
const char* mwinWaylandTextTypeName(int8_t index);
void mwinWaylandOfferText(const mwinWaylandPlatform* platform, void* source, uint32_t opcode);
void mwinWaylandAddSend(mwinWaylandPlatform* platform, int32_t fd, bool primary, int8_t item);
void mwinWaylandCloseSends(mwinWaylandClipboard* clipboard, bool primary);
void mwinWaylandAttachPrimary(mwinWaylandPlatform* platform);
void mwinWaylandDetachPrimary(mwinWaylandPlatform* platform);
int mwinWaylandWritePrimary(mwinWaylandPlatform* platform);
void mwinWaylandFailRead(mwinWaylandPlatform* platform);
void mwinWaylandPumpRead(mwinWaylandPlatform* platform, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_WAYLAND_CLIPBOARD_H
