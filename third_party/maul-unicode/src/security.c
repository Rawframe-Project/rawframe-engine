// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The script and number checks of UTS #39 section 5. Script sets are
// 256-bit masks over Script indexes, with the four writing systems that
// combine scripts (Hanb, Hntl, Jpan, Kore) in the top indexes. A code
// point of Common or Inherited script, without extensions, fits every
// script and leaves a set alone.

#include "maul-unicode/security.h"

#include "bits.h"
#include "encoding.h"
#include "tables.h"

#include <string.h>

typedef struct Scripts
{
    uint64_t bits[4];
} Scripts;

// The writing systems, in the indexes above every Script index.
enum
{
    IndexHanb = 252,
    IndexHntl = 253,
    IndexJpan = 254,
    IndexKore = 255,
};

static const uint32_t s_writingSystems[4] = {
    MUNI_SCRIPT('H', 'a', 'n', 'b'),
    MUNI_SCRIPT('H', 'n', 't', 'l'),
    MUNI_SCRIPT('J', 'p', 'a', 'n'),
    MUNI_SCRIPT('K', 'o', 'r', 'e'),
};

// The Recommended scripts of UAX #31 table 5 but Common, Inherited,
// Cyrillic and Greek: those a moderately restrictive string may add to
// Latin.
static const uint32_t s_moderateScripts[] = {
    MUNI_SCRIPT('A', 'r', 'a', 'b'), MUNI_SCRIPT('A', 'r', 'm', 'n'),
    MUNI_SCRIPT('B', 'e', 'n', 'g'), MUNI_SCRIPT('D', 'e', 'v', 'a'),
    MUNI_SCRIPT('E', 't', 'h', 'i'), MUNI_SCRIPT('G', 'e', 'o', 'r'),
    MUNI_SCRIPT('G', 'u', 'j', 'r'), MUNI_SCRIPT('G', 'u', 'r', 'u'),
    MUNI_SCRIPT('H', 'a', 'n', 'g'), MUNI_SCRIPT('H', 'a', 'n', 'i'),
    MUNI_SCRIPT('H', 'e', 'b', 'r'), MUNI_SCRIPT('H', 'i', 'r', 'a'),
    MUNI_SCRIPT('K', 'a', 'n', 'a'), MUNI_SCRIPT('K', 'n', 'd', 'a'),
    MUNI_SCRIPT('K', 'h', 'm', 'r'), MUNI_SCRIPT('L', 'a', 'o', 'o'),
    MUNI_SCRIPT('M', 'l', 'y', 'm'), MUNI_SCRIPT('M', 'y', 'm', 'r'),
    MUNI_SCRIPT('O', 'r', 'y', 'a'), MUNI_SCRIPT('S', 'i', 'n', 'h'),
    MUNI_SCRIPT('T', 'a', 'm', 'l'), MUNI_SCRIPT('T', 'e', 'l', 'u'),
    MUNI_SCRIPT('T', 'h', 'a', 'a'), MUNI_SCRIPT('T', 'h', 'a', 'i'),
    MUNI_SCRIPT('T', 'i', 'b', 't'),
};

// The Script indexes the augmentation and the levels ask about.
typedef struct Indexes
{
    uint8_t common;
    uint8_t inherited;
    uint8_t han;
    uint8_t hiragana;
    uint8_t katakana;
    uint8_t hangul;
    uint8_t bopomofo;
    uint8_t latin;
} Indexes;

static void Add(Scripts* scripts, unsigned index)
{
    scripts->bits[index >> 6] |= (uint64_t)1 << (index & 63);
}

static bool Has(const Scripts* scripts, unsigned index)
{
    return (scripts->bits[index >> 6] >> (index & 63) & 1) != 0;
}

static void Intersect(Scripts* scripts, const Scripts* other)
{
    for (int i = 0; i < 4; i++)
    {
        scripts->bits[i] &= other->bits[i];
    }
}

static bool IsEmpty(const Scripts* scripts)
{
    return (scripts->bits[0] | scripts->bits[1] | scripts->bits[2] | scripts->bits[3]) == 0;
}

static bool IsAll(const Scripts* scripts)
{
    return (scripts->bits[0] & scripts->bits[1] & scripts->bits[2] & scripts->bits[3]) ==
           UINT64_MAX;
}

// The Script index of a tag, by binary search; the tag must be present.
static uint8_t IndexOf(uint32_t tag)
{
    uint32_t low = 0;
    uint32_t high = muniScriptTagCount;
    while (high - low > 1)
    {
        uint32_t middle = (low + high) / 2;
        if (muniScriptTags[middle] <= tag)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return (uint8_t)low;
}

static Indexes FindIndexes(void)
{
    return (Indexes){
        IndexOf(MUNI_SCRIPT_COMMON),
        IndexOf(MUNI_SCRIPT_INHERITED),
        IndexOf(MUNI_SCRIPT('H', 'a', 'n', 'i')),
        IndexOf(MUNI_SCRIPT('H', 'i', 'r', 'a')),
        IndexOf(MUNI_SCRIPT('K', 'a', 'n', 'a')),
        IndexOf(MUNI_SCRIPT('H', 'a', 'n', 'g')),
        IndexOf(MUNI_SCRIPT('B', 'o', 'p', 'o')),
        IndexOf(MUNI_SCRIPT('L', 'a', 't', 'n')),
    };
}

// The augmented script set of a code point (UTS #39 section 5.1); false
// when it is every script.
static bool Augmented(uint32_t codePoint, const Indexes* indexes, Scripts* scriptsOut)
{
    uint8_t script = muniLookupScript(codePoint);
    uint8_t set = muniLookupScriptExtensions(codePoint);
    memset(scriptsOut, 0, sizeof(*scriptsOut));
    if (set == 0)
    {
        Add(scriptsOut, script);
    }
    for (size_t k = set == 0 ? 0 : muniScriptSetStarts[set - 1];
         set != 0 && k < muniScriptSetStarts[set]; k++)
    {
        Add(scriptsOut, muniScriptSetMembers[k]);
    }
    if (Has(scriptsOut, indexes->common) || Has(scriptsOut, indexes->inherited))
    {
        return false;
    }
    if (Has(scriptsOut, indexes->han))
    {
        Add(scriptsOut, IndexHanb);
        Add(scriptsOut, IndexHntl);
        Add(scriptsOut, IndexJpan);
        Add(scriptsOut, IndexKore);
    }
    if (Has(scriptsOut, indexes->hiragana) || Has(scriptsOut, indexes->katakana))
    {
        Add(scriptsOut, IndexJpan);
    }
    if (Has(scriptsOut, indexes->hangul))
    {
        Add(scriptsOut, IndexKore);
    }
    if (Has(scriptsOut, indexes->bopomofo))
    {
        Add(scriptsOut, IndexHanb);
    }
    if (Has(scriptsOut, indexes->latin))
    {
        Add(scriptsOut, IndexHntl);
    }
    return true;
}

// What one pass over the text learns.
typedef struct Survey
{
    Scripts resolved; // the intersection of every augmented set
    Scripts nonLatin; // the intersection of those without Latin
    bool ascii;       // every code point is ASCII
    bool allowed;     // the General Security Profile allows every one
} Survey;

static muniTextResult Examine(const char* text, size_t length, Survey* surveyOut)
{
    Indexes indexes = FindIndexes();
    memset(&surveyOut->resolved, 0xFF, sizeof(Scripts));
    memset(&surveyOut->nonLatin, 0xFF, sizeof(Scripts));
    surveyOut->ascii = true;
    surveyOut->allowed = true;
    const uint8_t* bytes = (const uint8_t*)text;
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = muniStepUtf8(bytes + offset, length - offset, &codePoint, &size);
        if (status != muni_success)
        {
            return (muniTextResult){status, offset};
        }
        offset += size;
        surveyOut->ascii = surveyOut->ascii && codePoint < 0x80;
        surveyOut->allowed = surveyOut->allowed && muniLookupSecurity(codePoint) != 0;
        Scripts scripts;
        if (!Augmented(codePoint, &indexes, &scripts))
        {
            continue;
        }
        Intersect(&surveyOut->resolved, &scripts);
        if (!Has(&scripts, indexes.latin))
        {
            Intersect(&surveyOut->nonLatin, &scripts);
        }
    }
    return (muniTextResult){muni_success, length};
}

// The tag of a set index.
static uint32_t TagOf(unsigned index)
{
    return index >= IndexHanb ? s_writingSystems[index - IndexHanb] : muniScriptTags[index];
}

muniTextResult muniGetResolvedScripts(const char* text, size_t length, muniScript* scripts,
                                      size_t capacity, size_t* countOut)
{
    if ((text == nullptr && length != 0) || (scripts == nullptr && capacity != 0) ||
        countOut == nullptr)
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    Survey survey;
    muniTextResult result = Examine(text, length, &survey);
    if (result.status != muni_success)
    {
        return result;
    }
    uint32_t tags[256];
    size_t count = 0;
    bool all = IsAll(&survey.resolved);
    if (all)
    {
        tags[count++] = MUNI_SCRIPT_COMMON;
    }
    for (unsigned word = 0; word < 4 && !all; word++)
    {
        for (uint64_t bits = survey.resolved.bits[word]; bits != 0; bits &= bits - 1)
        {
            // Insertion keeps the tags sorted, as the writing systems are
            // not in tag order.
            uint32_t tag = TagOf(word * 64 + muniLowestBit(bits));
            size_t at = count++;
            for (; at > 0 && tags[at - 1] > tag; at--)
            {
                tags[at] = tags[at - 1];
            }
            tags[at] = tag;
        }
    }
    *countOut = count;
    if (count > capacity)
    {
        return (muniTextResult){muni_errorCapacity, length};
    }
    if (count > 0)
    {
        memcpy(scripts, tags, sizeof(uint32_t) * count);
    }
    return (muniTextResult){muni_success, length};
}

static muniRestrictionLevel Grade(const Survey* survey)
{
    if (!survey->allowed)
    {
        return muni_restrictionUnrestricted;
    }
    if (survey->ascii)
    {
        return muni_restrictionAsciiOnly;
    }
    if (!IsEmpty(&survey->resolved))
    {
        return muni_restrictionSingleScript;
    }
    if (Has(&survey->nonLatin, IndexJpan) || Has(&survey->nonLatin, IndexHanb) ||
        Has(&survey->nonLatin, IndexKore))
    {
        return muni_restrictionHighlyRestrictive;
    }
    for (size_t i = 0; i < sizeof(s_moderateScripts) / sizeof(s_moderateScripts[0]); i++)
    {
        if (Has(&survey->nonLatin, IndexOf(s_moderateScripts[i])))
        {
            return muni_restrictionModeratelyRestrictive;
        }
    }
    return muni_restrictionMinimallyRestrictive;
}

muniTextResult muniGetRestrictionLevel(const char* text, size_t length,
                                       muniRestrictionLevel* levelOut)
{
    if ((text == nullptr && length != 0) || levelOut == nullptr)
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    Survey survey;
    muniTextResult result = Examine(text, length, &survey);
    if (result.status == muni_success)
    {
        *levelOut = Grade(&survey);
    }
    return result;
}

muniTextResult muniCheckMixedNumbers(const char* text, size_t length, bool* mixedOut)
{
    if ((text == nullptr && length != 0) || mixedOut == nullptr)
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    const uint8_t* bytes = (const uint8_t*)text;
    uint32_t system = UINT32_MAX; // the zero of the first digit's system
    bool mixed = false;
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = muniStepUtf8(bytes + offset, length - offset, &codePoint, &size);
        if (status != muni_success)
        {
            return (muniTextResult){status, offset};
        }
        offset += size;
        int32_t value = muniGetDecimalDigitValue(codePoint);
        if (value < 0)
        {
            continue;
        }
        uint32_t zero = codePoint - (uint32_t)value;
        mixed = mixed || (system != UINT32_MAX && zero != system);
        system = zero;
    }
    *mixedOut = mixed;
    return (muniTextResult){muni_success, length};
}

bool muniIsIdentifierAllowed(uint32_t codePoint)
{
    return muniLookupSecurity(codePoint) != 0;
}
