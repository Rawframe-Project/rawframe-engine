// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// POSIX locale names, language[_TERRITORY][.codeset][@modifier], as BCP
// 47 tags: the codeset goes, @latin and @cyrillic are scripts,
// @valencia is a variant, and other modifiers say nothing of the
// language.

#include "linux_locale.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// The longest tag made: a language of 3, a script, a territory of 3 and
// the variant.
#define TAG_BYTES 32

static bool IsLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

static char Lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static char Upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

// Whether a part of a name, of length count, is all letters or all
// digits as asked, and between two and three long.
static bool IsPart(const char* part, size_t count, bool digits)
{
    bool fits = count >= 2 && count <= 3;
    for (size_t i = 0; i < count && fits; i++)
    {
        fits = digits ? IsDigit(part[i]) : IsLetter(part[i]);
    }
    return fits;
}

static size_t Put(char* tag, size_t at, const char* text, size_t count)
{
    memcpy(tag + at, text, count);
    return at + count;
}

// The tag of one name of the given length, or 0 for a name that is no
// language's.
static size_t TagOf(const char* name, size_t length, char tag[TAG_BYTES])
{
    const char* end = name + length;
    const char* modifier = memchr(name, '@', length);
    const char* codeset = memchr(name, '.', length);
    const char* stop = codeset != nullptr ? codeset : modifier != nullptr ? modifier : end;
    const char* underscore = memchr(name, '_', (size_t)(stop - name));
    const char* territory = underscore != nullptr ? underscore + 1 : stop;
    size_t languageLength = (size_t)((underscore != nullptr ? underscore : stop) - name);
    size_t territoryLength = (size_t)(stop - territory);
    // A territory is two letters, or three digits of UN M.49 (es_419).
    if (!IsPart(name, languageLength, false) ||
        (underscore != nullptr && !IsPart(territory, territoryLength, territoryLength == 3)))
    {
        return 0;
    }
    size_t modifierLength = modifier != nullptr ? (size_t)(end - modifier - 1) : 0;
    bool latin = modifierLength == 5 && memcmp(modifier + 1, "latin", 5) == 0;
    bool cyrillic = modifierLength == 8 && memcmp(modifier + 1, "cyrillic", 8) == 0;
    bool valencia = modifierLength == 8 && memcmp(modifier + 1, "valencia", 8) == 0;
    size_t at = 0;
    for (size_t i = 0; i < languageLength; i++)
    {
        tag[at++] = Lower(name[i]);
    }
    at = latin ? Put(tag, at, "-Latn", 5) : cyrillic ? Put(tag, at, "-Cyrl", 5) : at;
    if (underscore != nullptr)
    {
        tag[at++] = '-';
        for (size_t i = 0; i < territoryLength; i++)
        {
            tag[at++] = Upper(territory[i]);
        }
    }
    return valencia ? Put(tag, at, "-valencia", 9) : at;
}

// Whether the list already has the tag.
static bool Has(const char* list, size_t length, const char* tag, size_t tagLength)
{
    for (size_t at = 0; at + tagLength <= length;)
    {
        const char* comma = memchr(list + at, ',', length - at);
        size_t next = comma != nullptr ? (size_t)(comma - list) : length;
        if (next - at == tagLength && memcmp(list + at, tag, tagLength) == 0)
        {
            return true;
        }
        at = next + 1;
    }
    return false;
}

// Adds the tags of a list of names separated by colons.
static size_t AddNames(const char* names, char* out, size_t length, size_t capacity)
{
    while (names != nullptr && *names != '\0')
    {
        const char* colon = strchr(names, ':');
        size_t nameLength = colon != nullptr ? (size_t)(colon - names) : strlen(names);
        char tag[TAG_BYTES];
        size_t tagLength = TagOf(names, nameLength, tag);
        size_t needed = tagLength + (length > 0 ? 1u : 0u);
        if (tagLength > 0 && !Has(out, length, tag, tagLength) && length + needed <= capacity)
        {
            out[length] = ',';
            length += length > 0 ? 1u : 0u;
            memcpy(out + length, tag, tagLength);
            length += tagLength;
        }
        names = colon != nullptr ? colon + 1 : nullptr;
    }
    return length;
}

size_t mwinLinuxLocalesOf(const char* language, const char* messages, char* out, size_t capacity)
{
    // glibc heeds LANGUAGE only for a messages locale that is not C,
    // which no locale set means too.
    bool plain = messages == nullptr || messages[0] == '\0' || strcmp(messages, "C") == 0 ||
                 strncmp(messages, "C.", 2) == 0 || strcmp(messages, "POSIX") == 0;
    if (plain)
    {
        return 0;
    }
    size_t length = AddNames(language, out, 0, capacity);
    return AddNames(messages, out, length, capacity);
}

static const char* Set(const char* name)
{
    const char* value = getenv(name);
    return value != nullptr && value[0] != '\0' ? value : nullptr;
}

size_t mwinLinuxLocales(char* out, size_t capacity)
{
    const char* messages = Set("LC_ALL");
    messages = messages != nullptr ? messages : Set("LC_MESSAGES");
    messages = messages != nullptr ? messages : Set("LANG");
    return mwinLinuxLocalesOf(Set("LANGUAGE"), messages, out, capacity);
}
