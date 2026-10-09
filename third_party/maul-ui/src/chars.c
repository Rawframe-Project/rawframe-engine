// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text built in a fixed buffer, numbers written by the library itself
// (record mui-0001): libc's printf would be 8 KB of the core's wasm for
// a handful of calls. A float is written as "%g" writes it, its digits
// rounded to nearest, ties to even, on the exact binary value: scaled by
// an exact power of ten, the scaling's own error found exactly by
// Dekker's product decides a tie it made. Beyond 10^22 either way the
// scaling rounds more than once, and the last digit may differ from
// printf's.

#include "chars.h"

#include <string.h>

static uint32_t WriteDigits(char* out, uint64_t value, uint32_t least)
{
    char reversed[20];
    uint32_t count = 0;
    do
    {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0 || count < least);
    for (uint32_t i = 0; i < count; i++)
    {
        out[i] = reversed[count - 1 - i];
    }
    return count;
}

static const double s_powers[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                  1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                  1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

// The error of a product, exactly: a * b - round(a * b), by Dekker's
// splitting, each half of 26 bits so their products are exact.
static double ProductError(double a, double b, double product)
{
    const double split = 134217729.0;
    double ta = a * split;
    double ah = ta - (ta - a);
    double al = a - ah;
    double tb = b * split;
    double bh = tb - (tb - b);
    double bl = b - bh;
    return ((ah * bh - product) + ah * bl + al * bh) + al * bl;
}

// A value times 10 to a power, and on which side of it the exact result
// lies (-1, 0 or 1): known for a power within 22, whose 10^power is
// exact; 0 beyond, where the steps round more than once.
static double TimesPowerOfTen(double value, int power, int* sideOut)
{
    *sideOut = 0;
    if (power >= 0 && power <= 22)
    {
        double product = value * s_powers[power];
        double error = ProductError(value, s_powers[power], product);
        *sideOut = (error > 0.0) - (error < 0.0);
        return product;
    }
    if (power < 0 && power >= -22)
    {
        double quotient = value / s_powers[-power];
        double back = quotient * s_powers[-power];
        double rest = (value - back) - ProductError(quotient, s_powers[-power], back);
        *sideOut = (rest > 0.0) - (rest < 0.0);
        return quotient;
    }
    while (power > 22)
    {
        value *= 1e22;
        power -= 22;
    }
    while (power < -22)
    {
        value /= 1e22;
        power += 22;
    }
    return power >= 0 ? value * s_powers[power] : value / s_powers[-power];
}

// The digits significant digits of a positive value, as an integer from
// 10^(digits-1) below 10^digits, rounded to nearest with ties to even
// on the exact value where it is known, and its decimal exponent.
static uint64_t Scale(double value, uint32_t digits, int* exponentOut)
{
    // A first guess of the decimal exponent from the binary one.
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    int binary = (int)((bits >> 52) & 0x7FF) - 1023;
    int exponent = (int)((double)binary * 0.30102999566398120);
    double low = s_powers[digits - 1];
    for (;;)
    {
        int side = 0;
        double scaled = TimesPowerOfTen(value, (int)digits - 1 - exponent, &side);
        double whole = __builtin_floor(scaled);
        double rounded = __builtin_rint(scaled);
        // A tie that only the product's rounding made: the exact value
        // lies to one side of it.
        if (scaled - whole == 0.5 && side != 0)
        {
            rounded = side > 0 ? whole + 1.0 : whole;
        }
        if (scaled < low && rounded < low)
        {
            exponent--;
            continue;
        }
        if (scaled >= low * 10.0)
        {
            exponent++;
            continue;
        }
        if (rounded >= low * 10.0)
        {
            *exponentOut = exponent + 1;
            return (uint64_t)low;
        }
        *exponentOut = exponent;
        return (uint64_t)rounded;
    }
}

// Writes a word's letters, no NUL, and returns how many.
static uint32_t WriteWord(char* out, const char* word)
{
    uint32_t count = 0;
    for (; word[count] != '\0'; count++)
    {
        out[count] = word[count];
    }
    return count;
}

// Writes a finite or infinite value or NaN as "%.*g" does, digits from 1
// to 15, and returns its length, at most 23.
static uint32_t WriteGeneral(char* out, double value, uint32_t digits)
{
    uint32_t length = 0;
    if (value != value)
    {
        return WriteWord(out, "nan");
    }
    if (__builtin_signbit(value))
    {
        out[length++] = '-';
        value = -value;
    }
    if (value == __builtin_inf())
    {
        return length + WriteWord(out + length, "inf");
    }
    if (value == 0.0)
    {
        out[length++] = '0';
        return length;
    }
    int exponent = 0;
    uint64_t mantissa = Scale(value, digits, &exponent);
    char figures[20];
    uint32_t count = WriteDigits(figures, mantissa, digits);
    while (count > 1 && figures[count - 1] == '0')
    {
        count--;
    }
    if (exponent < -4 || exponent >= (int)digits)
    {
        out[length++] = figures[0];
        if (count > 1)
        {
            out[length++] = '.';
            memcpy(out + length, figures + 1, count - 1);
            length += count - 1;
        }
        out[length++] = 'e';
        out[length++] = exponent < 0 ? '-' : '+';
        length += WriteDigits(out + length, (uint64_t)(exponent < 0 ? -exponent : exponent), 2);
        return length;
    }
    if (exponent < 0)
    {
        out[length++] = '0';
        out[length++] = '.';
        for (int i = -1; i > exponent; i--)
        {
            out[length++] = '0';
        }
        memcpy(out + length, figures, count);
        return length + count;
    }
    uint32_t whole = (uint32_t)exponent + 1;
    for (uint32_t i = 0; i < whole; i++)
    {
        out[length++] = i < count ? figures[i] : '0';
    }
    if (count > whole)
    {
        out[length++] = '.';
        memcpy(out + length, figures + whole, count - whole);
        length += count - whole;
    }
    return length;
}

// Room for count more bytes and the NUL, as many as fit.
static size_t Room(const muiChars* chars, size_t count)
{
    size_t left = chars->capacity > chars->length + 1 ? chars->capacity - chars->length - 1 : 0;
    return count < left ? count : left;
}

static void Put(muiChars* chars, const char* bytes, size_t count)
{
    size_t room = Room(chars, count);
    if (room != 0)
    {
        memcpy(chars->data + chars->length, bytes, room);
        chars->length += room;
    }
    if (chars->capacity != 0)
    {
        chars->data[chars->length] = '\0';
    }
}

muiChars muiCharsIn(char* data, size_t capacity)
{
    if (capacity != 0)
    {
        data[0] = '\0';
    }
    return (muiChars){data, capacity, 0};
}

void muiPutText(muiChars* chars, const char* text)
{
    Put(chars, text, strlen(text));
}

void muiPutUnsigned(muiChars* chars, uint64_t value)
{
    char digits[20];
    Put(chars, digits, WriteDigits(digits, value, 1));
}

void muiPutHex(muiChars* chars, uint64_t value, uint32_t least, bool upper)
{
    const char* figures = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[16];
    uint32_t count = 0;
    do
    {
        reversed[count++] = figures[value & 15u];
        value >>= 4;
    } while (value != 0 || count < least);
    char digits[16];
    for (uint32_t i = 0; i < count; i++)
    {
        digits[i] = reversed[count - 1 - i];
    }
    Put(chars, digits, count);
}

void muiPutGeneral(muiChars* chars, double value, uint32_t digits)
{
    char text[32];
    Put(chars, text, WriteGeneral(text, value, digits < 1 ? 1 : digits > 15 ? 15 : digits));
}
