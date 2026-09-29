// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// text/uri-list.

#include "uri_list.h"

#include <string.h>

static int HexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// Decodes %XX in place: the new length, or 0 for a broken escape.
static size_t Decode(char* text, size_t length)
{
    size_t written = 0;
    for (size_t i = 0; i < length; i++)
    {
        if (text[i] != '%')
        {
            text[written++] = text[i];
            continue;
        }
        int high = i + 2 < length ? HexValue(text[i + 1]) : -1;
        int low = i + 2 < length ? HexValue(text[i + 2]) : -1;
        if (high < 0 || low < 0)
        {
            return 0;
        }
        text[written++] = (char)(high * 16 + low);
        i += 2;
    }
    return written;
}

// The path of a file URI, decoded in place: its length, or 0 when the
// line names no local file.
size_t mwinFileUriPath(char* line, size_t length, char** pathOut)
{
    static const char scheme[] = "file://";
    static const char local[] = "localhost";
    size_t prefix = sizeof(scheme) - 1;
    if (length <= prefix || memcmp(line, scheme, prefix) != 0)
    {
        return 0;
    }
    char* path = line + prefix;
    size_t rest = length - prefix;
    if (rest > sizeof(local) - 1 && memcmp(path, local, sizeof(local) - 1) == 0)
    {
        path += sizeof(local) - 1;
        rest -= sizeof(local) - 1;
    }
    if (path[0] != '/')
    {
        return 0;
    }
    *pathOut = path;
    return Decode(path, rest);
}

void mwinGatherUriList(mwinContext* context, char* list, size_t length)
{
    size_t start = 0;
    while (start < length)
    {
        const char* end = memchr(list + start, '\n', length - start);
        size_t next = end != nullptr ? (size_t)(end - list) + 1 : length;
        size_t line = (end != nullptr ? (size_t)(end - list) : length) - start;
        if (line > 0 && list[start + line - 1] == '\r')
        {
            line -= 1;
        }
        char* path = nullptr;
        size_t pathLength =
            line > 0 && list[start] != '#' ? mwinFileUriPath(list + start, line, &path) : 0;
        if (pathLength > 0)
        {
            mwinAddDroppedFile(context, path, pathLength);
        }
        else if (line > 0 && list[start] != '#')
        {
            context->dropping.truncated = true;
        }
        start = next;
    }
}
