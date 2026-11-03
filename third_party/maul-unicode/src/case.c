// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Case mapping and folding (Unicode chapter 3.13). The simple mappings
// come from each code point's record; the full ones from the special
// table when the record flags one. Two kinds of rule need context and
// live here: Final_Sigma, the Turkic rules for I, İ and the dot above
// (SpecialCasing.txt, tr and az), and the Lithuanian rules that keep the
// dot of an i under other accents (lt), Greek uppercasing (el) and the
// Armenian ligature (hy). Titlecasing walks the text's word boundaries
// (UAX #29).

#include "maul-unicode/case.h"

#include "case_map.h"
#include "encoding.h"
#include "overlap.h"
#include "tables.h"
#include "writer.h"

#include "maul-unicode/segment.h"

#define CAPITAL_I         0x0049u
#define SMALL_I           0x0069u
#define CAPITAL_I_DOT     0x0130u
#define SMALL_DOTLESS_I   0x0131u
#define COMBINING_DOT     0x0307u
#define CAPITAL_J         0x004Au
#define CAPITAL_I_OGONEK  0x012Eu
#define CAPITAL_I_GRAVE   0x00CCu
#define CAPITAL_I_ACUTE   0x00CDu
#define CAPITAL_I_TILDE   0x0128u
#define COMBINING_GRAVE   0x0300u
#define COMBINING_ACUTE   0x0301u
#define COMBINING_TILDE   0x0303u
#define CAPITAL_SIGMA     0x03A3u
#define SMALL_SIGMA       0x03C3u
#define SMALL_FINAL_SIGMA 0x03C2u
#define CAPITAL_ETA       0x0397u
#define CAPITAL_ETA_TONOS 0x0389u
#define CAPITAL_IOTA      0x0399u
#define CAPITAL_UPSILON   0x03A5u
#define IOTA_DIALYTIKA    0x03AAu
#define UPSILON_DIALYTIKA 0x03ABu
#define DIALYTIKA         0x0308u
#define ECH_YIWN          0x0587u
#define CAPITAL_ECH       0x0535u
#define CAPITAL_VEW       0x054Eu
#define SMALL_VEW         0x057Eu

static uint32_t Simple(uint32_t codePoint, int kind)
{
    return (uint32_t)((int32_t)codePoint + muniCaseDeltas[muniCaseRecord(codePoint)[kind]]);
}

uint32_t muniGetLowercase(uint32_t codePoint)
{
    return Simple(codePoint, muni_caseKindLower);
}

uint32_t muniGetUppercase(uint32_t codePoint)
{
    return Simple(codePoint, muni_caseKindUpper);
}

uint32_t muniGetTitlecase(uint32_t codePoint)
{
    return Simple(codePoint, muni_caseKindTitle);
}

uint32_t muniFoldCase(uint32_t codePoint)
{
    return Simple(codePoint, muni_caseKindFold);
}

bool muniIsCased(uint32_t codePoint)
{
    return (muniCaseRecord(codePoint)[4] & muni_caseFlagCased) != 0;
}

// Writes the full mapping of a code point that needs no context.
static void PutMapping(muniWriter* writer, uint32_t codePoint, int kind)
{
    uint32_t mapping[MUNI_MAX_CASE_MAPPING];
    size_t count = muniMapCaseFully(codePoint, kind, mapping);
    for (size_t i = 0; i < count; i++)
    {
        muniWriterPut(writer, mapping[i]);
    }
}

// The text being converted and the context the conditional rules track.
typedef struct Converter
{
    const uint8_t* text;
    size_t length;
    bool turkic;
    bool lithuanian;
    bool greek;
    bool armenian;
    bool casedBefore; // a cased letter, then only case-ignorables (Final_Sigma)
    bool afterI;      // an I, then nothing of class 0 or 230 (After_I)
    bool afterSoft;   // a Soft_Dotted letter, then the same (After_Soft_Dotted)
    // The accent the Greek vowel before dropped, a GreekAccent value.
    uint8_t lostAccent;
    muniWriter writer;
} Converter;

static uint32_t Decode(const Converter* converter, size_t offset, size_t* sizeOut)
{
    uint32_t codePoint;
    (void)muniStepUtf8(converter->text + offset, converter->length - offset, &codePoint, sizeOut);
    return codePoint;
}

// Final_Sigma after the sigma: no cased letter follows past case-ignorables.
static bool EndsWord(const Converter* converter, size_t offset)
{
    while (offset < converter->length)
    {
        size_t size;
        uint8_t flags = muniCaseRecord(Decode(converter, offset, &size))[4];
        if ((flags & muni_caseFlagCased) != 0)
        {
            return false;
        }
        if ((flags & muni_caseFlagIgnorable) == 0)
        {
            return true;
        }
        offset += size;
    }
    return true;
}

// Before_Dot: a U+0307 follows, past marks of classes other than 0 and 230.
static bool BeforeDot(const Converter* converter, size_t offset)
{
    while (offset < converter->length)
    {
        size_t size;
        uint32_t codePoint = Decode(converter, offset, &size);
        if (codePoint == COMBINING_DOT)
        {
            return true;
        }
        uint8_t markClass = muniLookupCombiningClass(codePoint);
        if (markClass == 0 || markClass == 230)
        {
            return false;
        }
        offset += size;
    }
    return false;
}

// More_Above: a mark of class 230 follows, past marks of other classes
// but 0.
static bool MoreAbove(const Converter* converter, size_t offset)
{
    while (offset < converter->length)
    {
        size_t size;
        uint8_t markClass = muniLookupCombiningClass(Decode(converter, offset, &size));
        if (markClass == 0 || markClass == 230)
        {
            return markClass == 230;
        }
        offset += size;
    }
    return false;
}

static bool IsSoftDotted(uint32_t codePoint)
{
    uint32_t low = 0;
    uint32_t high = muniSoftDottedCount;
    while (low < high)
    {
        uint32_t middle = (low + high) / 2;
        uint32_t first = muniSoftDotted[middle] & 0x1FFFFF;
        if (codePoint < first)
        {
            high = middle;
        }
        else if (codePoint > first + (muniSoftDotted[middle] >> 21))
        {
            low = middle + 1;
        }
        else
        {
            return true;
        }
    }
    return false;
}

// Lithuanian lowercasing keeps the dot of an i or a j that other accents
// above follow, writing it out; true when it applied.
static bool PutLithuanianLower(Converter* converter, uint32_t codePoint, size_t next)
{
    uint32_t accent = codePoint == CAPITAL_I_GRAVE   ? COMBINING_GRAVE
                      : codePoint == CAPITAL_I_ACUTE ? COMBINING_ACUTE
                      : codePoint == CAPITAL_I_TILDE ? COMBINING_TILDE
                                                     : 0;
    if (accent != 0)
    {
        muniWriterPut(&converter->writer, SMALL_I);
        muniWriterPut(&converter->writer, COMBINING_DOT);
        muniWriterPut(&converter->writer, accent);
        return true;
    }
    if ((codePoint == CAPITAL_I || codePoint == CAPITAL_J || codePoint == CAPITAL_I_OGONEK) &&
        MoreAbove(converter, next))
    {
        muniWriterPut(&converter->writer, muniGetLowercase(codePoint));
        muniWriterPut(&converter->writer, COMBINING_DOT);
        return true;
    }
    return false;
}

// The accent a Greek vowel dropped, which the next iota or upsilon
// answers with a dialytika of the same form.
enum
{
    GreekAccentNone = 0,
    GreekAccentPrecomposed = 1,
    GreekAccentCombining = 2,
};

// A combining dialytika that follows a Greek letter, as against the one
// its decomposition holds, and another mark taken with the letter and
// dropped.
#define GREEK_COMBINING_DIALYTIKA 0x10000u
#define GREEK_OTHER_MARK          0x20000u

// The Greek letter entry of a code point, or 0 for none.
static uint32_t GreekLetter(uint32_t codePoint)
{
    if (codePoint - 0x370u < 0x90u)
    {
        return muniGreekLetterValues[muniGreekLetters[codePoint - 0x370u]];
    }
    if (codePoint - 0x1F00u < 0x100u)
    {
        return muniGreekLetterValues[muniGreekLetters[0x90u + codePoint - 0x1F00u]];
    }
    return codePoint == 0x2126u ? muniGreekLetterValues[muniGreekLetters[0x190]] : 0;
}

// What a combining mark after a Greek letter adds to its entry, or 0 for
// a code point Greek uppercasing does not take with the letter. Marks
// that look like a perispomeni count as one.
static uint32_t GreekMark(uint32_t codePoint)
{
    switch (codePoint)
    {
    case 0x0300: // varia
    case 0x0301: // tonos, oxia
    case 0x0302: // circumflex
    case 0x0303: // tilde
    case 0x0311: // inverted breve
    case 0x0342: // perispomeni
        return muni_greekAccent;
    case 0x0308: // dialytika
        return GREEK_COMBINING_DIALYTIKA;
    case 0x0344: // dialytika tonos
        return GREEK_COMBINING_DIALYTIKA | muni_greekAccent;
    case 0x0345: // ypogegrammeni
        return muni_greekYpogegrammeni;
    case 0x0304: // macron
    case 0x0306: // vrachy
    case 0x0313: // psili
    case 0x0314: // dasia
    case 0x0343: // koronis
        return GREEK_OTHER_MARK;
    default:
        return 0;
    }
}

// Takes the combining marks after a Greek letter, up to end, into its
// entry and counts its iota subscripts; returns the offset past them.
static size_t TakeGreekMarks(const Converter* converter, uint32_t* entry, size_t* subscripts,
                             size_t next, size_t end)
{
    while (next < end)
    {
        size_t size;
        uint32_t mark = GreekMark(Decode(converter, next, &size));
        if (mark == 0)
        {
            break;
        }
        *entry |= mark;
        *subscripts += (mark & muni_greekYpogegrammeni) != 0 ? 1 : 0;
        next += size;
    }
    return next;
}

// Uppercases a Greek letter with its entry and takes the combining marks
// after it, up to end, returning the offset past them. This follows ICU's
// Greek uppercasing, which CLDR's el-Upper describes: accents and
// breathings go, a dialytika stays, an iota subscript becomes a capital
// iota, and the eta that means "or" keeps its accent.
static size_t PutGreekUpper(Converter* converter, uint32_t entry, size_t next, size_t end)
{
    uint32_t upper = entry & muni_greekUpper;
    if ((entry & muni_greekVowel) != 0 && converter->lostAccent != GreekAccentNone &&
        (upper == CAPITAL_IOTA || upper == CAPITAL_UPSILON))
    {
        entry |= converter->lostAccent == GreekAccentPrecomposed ? muni_greekDialytika
                                                                 : GREEK_COMBINING_DIALYTIKA;
    }
    bool precomposedAccent = (entry & muni_greekAccent) != 0;
    size_t subscripts = (entry & muni_greekYpogegrammeni) != 0 ? 1 : 0;
    next = TakeGreekMarks(converter, &entry, &subscripts, next, end);
    uint32_t vowelAccent = muni_greekVowel | muni_greekAccent;
    converter->lostAccent = (entry & (vowelAccent | muni_greekDialytika)) != vowelAccent
                                ? GreekAccentNone
                            : precomposedAccent ? GreekAccentPrecomposed
                                                : GreekAccentCombining;
    uint32_t dialytika = muni_greekDialytika | GREEK_COMBINING_DIALYTIKA;
    bool tonos = false;
    if (upper == CAPITAL_ETA && (entry & muni_greekAccent) != 0 && subscripts == 0 &&
        !converter->casedBefore && EndsWord(converter, next))
    {
        // The disjunctive eta, alone between words, keeps its tonos.
        upper = precomposedAccent ? CAPITAL_ETA_TONOS : upper;
        tonos = !precomposedAccent;
    }
    else if ((entry & muni_greekDialytika) != 0 &&
             (upper == CAPITAL_IOTA || upper == CAPITAL_UPSILON))
    {
        upper = upper == CAPITAL_IOTA ? IOTA_DIALYTIKA : UPSILON_DIALYTIKA;
        entry &= ~dialytika;
    }
    muniWriterPut(&converter->writer, upper);
    if ((entry & dialytika) != 0)
    {
        muniWriterPut(&converter->writer, DIALYTIKA);
    }
    if (tonos)
    {
        muniWriterPut(&converter->writer, COMBINING_ACUTE);
    }
    for (; subscripts > 0; subscripts--)
    {
        muniWriterPut(&converter->writer, CAPITAL_IOTA);
    }
    return next;
}

// Lowercases one code point at offset, whose encoding ends at next.
static void PutLower(Converter* converter, uint32_t codePoint, size_t next)
{
    if (codePoint == CAPITAL_SIGMA)
    {
        bool final = converter->casedBefore && EndsWord(converter, next);
        muniWriterPut(&converter->writer, final ? SMALL_FINAL_SIGMA : SMALL_SIGMA);
        return;
    }
    if (converter->turkic)
    {
        if (codePoint == CAPITAL_I_DOT)
        {
            muniWriterPut(&converter->writer, SMALL_I);
            return;
        }
        if (codePoint == CAPITAL_I)
        {
            muniWriterPut(&converter->writer,
                          BeforeDot(converter, next) ? SMALL_I : SMALL_DOTLESS_I);
            return;
        }
        if (codePoint == COMBINING_DOT && converter->afterI)
        {
            return; // the dot of an I, which lowercasing made an i
        }
    }
    if (converter->lithuanian && PutLithuanianLower(converter, codePoint, next))
    {
        return;
    }
    PutMapping(&converter->writer, codePoint, muni_caseKindLower);
}

// Uppercases or titlecases one code point.
static void PutUpper(Converter* converter, uint32_t codePoint, int kind)
{
    if (converter->turkic && codePoint == SMALL_I)
    {
        muniWriterPut(&converter->writer, CAPITAL_I_DOT);
        return;
    }
    if (converter->lithuanian && codePoint == COMBINING_DOT && converter->afterSoft)
    {
        return; // the dot of an i, which the capital does not need
    }
    if (converter->armenian && codePoint == ECH_YIWN)
    {
        // Ech and vew, as the reformed spelling writes the ligature.
        muniWriterPut(&converter->writer, CAPITAL_ECH);
        muniWriterPut(&converter->writer, kind == muni_caseKindUpper ? CAPITAL_VEW : SMALL_VEW);
        return;
    }
    PutMapping(&converter->writer, codePoint, kind);
}

static void PutFold(Converter* converter, uint32_t codePoint)
{
    if (converter->turkic && (codePoint == CAPITAL_I || codePoint == CAPITAL_I_DOT))
    {
        muniWriterPut(&converter->writer, codePoint == CAPITAL_I ? SMALL_DOTLESS_I : SMALL_I);
        return;
    }
    PutMapping(&converter->writer, codePoint, muni_caseKindFold);
}

// Moves the context past a code point. Folding needs none.
static void Track(Converter* converter, uint32_t codePoint)
{
    uint8_t flags = muniCaseRecord(codePoint)[4];
    if ((flags & muni_caseFlagCased) != 0)
    {
        converter->casedBefore = true;
    }
    else if ((flags & muni_caseFlagIgnorable) == 0)
    {
        converter->casedBefore = false;
    }
    if (!converter->turkic && !converter->lithuanian)
    {
        return; // only the language rules look back past marks
    }
    uint8_t markClass = codePoint < 0x300 ? 0 : muniLookupCombiningClass(codePoint);
    if (markClass == 0 || markClass == 230)
    {
        converter->afterI = codePoint == CAPITAL_I;
        converter->afterSoft = converter->lithuanian && IsSoftDotted(codePoint);
    }
}

// Converts text[start, end): with titleFirst, the first cased code point
// is titlecased and the rest lowercased. Returns the offset of the first
// ill-formed sequence in strict mode, or end.
static size_t Convert(Converter* converter, size_t start, size_t end, muniCaseOperation operation,
                      bool strict, muniResult* statusOut)
{
    bool titleDone = false;
    size_t offset = start;
    while (offset < end)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status =
            muniStepUtf8(converter->text + offset, converter->length - offset, &codePoint, &size);
        if (status != muni_success && strict)
        {
            *statusOut = status;
            return offset;
        }
        size_t next = offset + size;
        uint32_t greek =
            converter->greek && operation == muni_caseUpper ? GreekLetter(codePoint) : 0;
        if (greek != 0)
        {
            next = PutGreekUpper(converter, greek, next, end);
        }
        else if (operation == muni_caseUpper)
        {
            converter->lostAccent = GreekAccentNone;
            PutUpper(converter, codePoint, muni_caseKindUpper);
        }
        else if (operation == muni_caseFold)
        {
            PutFold(converter, codePoint);
        }
        else if (operation == muni_caseTitle && !titleDone && muniIsCased(codePoint))
        {
            PutUpper(converter, codePoint, muni_caseKindTitle);
            titleDone = true;
        }
        else
        {
            PutLower(converter, codePoint, next);
        }
        if (operation != muni_caseFold)
        {
            Track(converter, codePoint);
        }
        offset = next;
    }
    return end;
}

muniTextResult muniConvertCase(const char* text, size_t length, muniCaseOperation operation,
                               muniCaseLanguage language, muniConvertMode mode, char* textOut,
                               size_t capacity, size_t* neededOut)
{
    if ((text == nullptr && length != 0) || (textOut == nullptr && capacity != 0) ||
        neededOut == nullptr || (uint32_t)operation > muni_caseFold ||
        (uint32_t)language > muni_caseArmenian ||
        (mode != muni_convertStrict && mode != muni_convertReplace) ||
        muniOverlaps(text, length, textOut, capacity, 1))
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    Converter converter = {.text = (const uint8_t*)text,
                           .length = length,
                           .turkic = language == muni_caseTurkic,
                           .lithuanian = language == muni_caseLithuanian,
                           .greek = language == muni_caseGreek,
                           .armenian = language == muni_caseArmenian,
                           .writer = {textOut, capacity, 0}};
    bool strict = mode == muni_convertStrict;
    muniResult status = muni_success;
    size_t stop = length;
    if (operation != muni_caseTitle)
    {
        stop = Convert(&converter, 0, length, operation, strict, &status);
    }
    else
    {
        // Each word, from one boundary to the next.
        muniSegmentIterator words;
        size_t start = 0;
        size_t end = 0;
        bool started = muniInitWordIterator(&words, text, length, false) == muni_success;
        while (started && status == muni_success &&
               muniSegmentIterator_NextBreak(&words, &end) == muni_success)
        {
            stop = Convert(&converter, start, end, operation, strict, &status);
            start = end;
        }
    }
    *neededOut = converter.writer.needed;
    if (status != muni_success)
    {
        return (muniTextResult){status, stop};
    }
    return (muniTextResult){!muniWriterFits(&converter.writer) ? muni_errorCapacity : muni_success,
                            length};
}
