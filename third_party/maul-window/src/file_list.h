// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A list of paths as drops and dialogs deliver them: UTF-8, each ended
// by a NUL, in one block from the allocator that grows as paths come,
// bounded by a count and a number of bytes.

#ifndef MAUL_WINDOW_SRC_FILE_LIST_H
#define MAUL_WINDOW_SRC_FILE_LIST_H

#include "maul-window/base.h"

typedef struct mwinFileList
{
    char* bytes;
    uint32_t length;
    uint32_t capacity;
    uint32_t count;
} mwinFileList;

// How adding a path went.
typedef enum mwinListResult
{
    mwin_listAdded,
    // Empty, holding a NUL, or not UTF-8 (UTF-16): no file the program
    // could open.
    mwin_listNotPath,
    // Past the count or the bytes, or the allocator's room.
    mwin_listFull,
} mwinListResult;

typedef struct mwinListBounds
{
    const mwinAllocator* allocator;
    uint32_t count;
    uint32_t bytes;
} mwinListBounds;

mwinListResult mwinListAdd(mwinFileList* list, mwinListBounds bounds, const char* path,
                           size_t length);
mwinListResult mwinListAddUtf16(mwinFileList* list, mwinListBounds bounds, const uint16_t* path,
                                size_t length);

void mwinListRelease(mwinFileList* list, const mwinAllocator* allocator);

#endif // MAUL_WINDOW_SRC_FILE_LIST_H
