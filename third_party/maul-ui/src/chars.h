// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text built in a fixed buffer, and numbers written into it, without the
// C library's printf (record mui-0001).

#ifndef MAUL_UI_SRC_CHARS_H
#define MAUL_UI_SRC_CHARS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A buffer of capacity bytes being written: length of them so far, NUL
// after them; what does not fit is dropped, as snprintf drops it.
typedef struct muiChars
{
    char* data;
    size_t capacity;
    size_t length;
} muiChars;

// An empty text in a buffer.
muiChars muiCharsIn(char* data, size_t capacity);

void muiPutText(muiChars* chars, const char* text);

// A value in decimal.
void muiPutUnsigned(muiChars* chars, uint64_t value);

// A value in hexadecimal, at least least digits, upper case when upper.
void muiPutHex(muiChars* chars, uint64_t value, uint32_t least, bool upper);

// A value as "%.*g" writes it with digits significant digits, 1 to 15,
// which a double holds as an integer exactly.
void muiPutGeneral(muiChars* chars, double value, uint32_t digits);

#endif // MAUL_UI_SRC_CHARS_H
