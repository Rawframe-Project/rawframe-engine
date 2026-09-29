// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A list of paths.

#include "file_list.h"

#include "allocator.h"

#include "maul-unicode/encoding.h"

#include <string.h>

// Room for a path of a length and its NUL: where it goes, or NULL past
// the bounds or the allocator's room.
static char* Room(mwinFileList* list, mwinListBounds bounds, size_t length)
{
    size_t needed = (size_t)list->length + length + 1;
    if (list->count >= bounds.count || needed > bounds.bytes)
    {
        return nullptr;
    }
    if (needed > list->capacity)
    {
        size_t capacity = (size_t)list->capacity * 2;
        capacity = capacity < needed ? needed : capacity;
        capacity = capacity < bounds.bytes ? capacity : bounds.bytes;
        char* grown = mwinAllocate(bounds.allocator, capacity, 1);
        if (grown == nullptr)
        {
            return nullptr;
        }
        if (list->bytes != nullptr)
        {
            memcpy(grown, list->bytes, list->length);
            mwinRelease(bounds.allocator, list->bytes, list->capacity, 1);
        }
        list->bytes = grown;
        list->capacity = (uint32_t)capacity;
    }
    return list->bytes + list->length;
}

// Counts a path written where Room said, and ends it.
static void Count(mwinFileList* list, size_t length)
{
    list->bytes[list->length + length] = '\0';
    list->length += (uint32_t)length + 1;
    list->count += 1;
}

mwinListResult mwinListAdd(mwinFileList* list, mwinListBounds bounds, const char* path,
                           size_t length)
{
    if (length == 0 || memchr(path, '\0', length) != nullptr ||
        muniValidateUtf8(path, length).status != muni_success)
    {
        return mwin_listNotPath;
    }
    char* room = Room(list, bounds, length);
    if (room == nullptr)
    {
        return mwin_listFull;
    }
    memcpy(room, path, length);
    Count(list, length);
    return mwin_listAdded;
}

mwinListResult mwinListAddUtf16(mwinFileList* list, mwinListBounds bounds, const uint16_t* path,
                                size_t length)
{
    size_t needed = 0;
    muniTextResult converted =
        muniConvertUtf16ToUtf8(path, length, nullptr, 0, muni_convertStrict, &needed);
    bool nul = false;
    for (size_t i = 0; i < length && !nul; i++)
    {
        nul = path[i] == 0;
    }
    if (length == 0 || nul || converted.status == muni_errorUtf16Surrogate)
    {
        return mwin_listNotPath;
    }
    char* room = Room(list, bounds, needed);
    if (room == nullptr)
    {
        return mwin_listFull;
    }
    (void)muniConvertUtf16ToUtf8(path, length, room, needed, muni_convertStrict, &needed);
    Count(list, needed);
    return mwin_listAdded;
}

void mwinListRelease(mwinFileList* list, const mwinAllocator* allocator)
{
    if (list->bytes != nullptr)
    {
        mwinRelease(allocator, list->bytes, list->capacity, 1);
    }
    *list = (mwinFileList){0};
}
