// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The property tables tools/munigen writes into src/generated/. Each
// property has a lookup with its layout compiled in, and the hash of
// its value for every code point as the generator computed it from the
// UCD: FNV-1a 64 over one byte per code point from U+0000 to U+10FFFF.

#ifndef MAUL_UNICODE_SRC_TABLES_H
#define MAUL_UNICODE_SRC_TABLES_H

#include <stdint.h>

uint8_t muniLookupGeneralCategory(uint32_t codePoint);
extern const uint64_t muniGeneralCategoryHash;
uint8_t muniLookupGraphemeClusterBreak(uint32_t codePoint);
extern const uint64_t muniGraphemeClusterBreakHash;
uint8_t muniLookupWordBreak(uint32_t codePoint);
extern const uint64_t muniWordBreakHash;
uint8_t muniLookupSentenceBreak(uint32_t codePoint);
extern const uint64_t muniSentenceBreakHash;
uint8_t muniLookupIndicConjunctBreak(uint32_t codePoint);
extern const uint64_t muniIndicConjunctBreakHash;
// The emoji properties of UTS #51 as the muni_emoji bits below.
uint8_t muniLookupEmoji(uint32_t codePoint);
extern const uint64_t muniEmojiHash;
uint8_t muniLookupWhiteSpace(uint32_t codePoint);
extern const uint64_t muniWhiteSpaceHash;
uint8_t muniLookupDefaultIgnorable(uint32_t codePoint);
extern const uint64_t muniDefaultIgnorableHash;
// The zeros of the decimal digit systems, sorted: a digit's value is its
// distance from the greatest zero at or below it.
extern const uint32_t muniDecimalZeros[];
extern const uint16_t muniDecimalZeroCount;

enum
{
    muni_emojiExtendedPictographic = 1,
    muni_emojiEmoji = 2,
    muni_emojiPresentation = 4,
    muni_emojiModifier = 8,
    muni_emojiModifierBase = 16,
    muni_emojiComponent = 32,
};

uint8_t muniLookupLineBreak(uint32_t codePoint);
extern const uint64_t muniLineBreakHash;
uint8_t muniLookupEastAsianWidth(uint32_t codePoint);
extern const uint64_t muniEastAsianWidthHash;
uint8_t muniLookupBidiClass(uint32_t codePoint);
extern const uint64_t muniBidiClassHash;
uint8_t muniLookupCombiningClass(uint32_t codePoint);
extern const uint64_t muniCombiningClassHash;
// The Script table stores an index into muniScriptTags.
uint8_t muniLookupScript(uint32_t codePoint);
extern const uint64_t muniScriptHash;
extern const uint32_t muniScriptTags[];
extern const uint16_t muniScriptTagCount;
// The ScriptExtensions table stores 0 when a code point's extensions are
// its Script alone, or 1 plus the index of a set: set i holds the Script
// indexes muniScriptSetMembers[muniScriptSetStarts[i]] up to
// muniScriptSetStarts[i + 1].
uint8_t muniLookupScriptExtensions(uint32_t codePoint);
extern const uint64_t muniScriptExtensionsHash;
extern const uint16_t muniScriptSetStarts[];
extern const uint8_t muniScriptSetMembers[];

// The BidiMirror table stores 0, or 1 plus an index into
// muniBidiMirrorDeltas, the distance to the Bidi_Mirroring_Glyph.
uint8_t muniLookupBidiMirror(uint32_t codePoint);
extern const uint64_t muniBidiMirrorHash;
extern const int32_t muniBidiMirrorDeltas[];
// Bidi_Paired_Bracket_Type: 0 none, 1 open, 2 close. The paired bracket
// is the Bidi_Mirroring_Glyph.
uint8_t muniLookupBidiBracket(uint32_t codePoint);
extern const uint64_t muniBidiBracketHash;

// Identifier properties as bits: 1 XID_Start, 2 XID_Continue,
// 4 Pattern_Syntax, 8 Pattern_White_Space.
uint8_t muniLookupIdentifier(uint32_t codePoint);
extern const uint64_t muniIdentifierHash;

// Normalization data, found through rank indexes: the block table gives
// a code point's 64-code-point block 1 plus its number, or 0 when the
// block holds no mapping; the block's bit map marks the code points with
// one, and the block's rank plus the marked code points before it is the
// mapping's position. Starts holds each block's first code point >> 6.
uint8_t muniLookupDecompositionBlock(uint32_t codePoint);
extern const uint64_t muniDecompositionBlockHash;
extern const uint16_t muniDecompositionStarts[];
extern const uint16_t muniDecompositionBlockCount;
// Canonical pairs: first << 7 | mark index, bit 28 set when excluded
// from composition; in code point order.
extern const uint64_t muniDecompositionPairBits[];
extern const uint16_t muniDecompositionPairRanks[];
extern const uint32_t muniDecompositionPairs[];
// The second code points of pairs, sorted; bit 31 set on those that
// compose with what precedes them.
extern const uint32_t muniDecompositionMarks[];
extern const uint16_t muniDecompositionMarkCount;
// The pairs that compose, as positions in muniDecompositionPairs, sorted
// by their first code point and mark.
extern const uint16_t muniCompositionOrder[];
extern const uint16_t muniCompositionCount;
// Canonical singletons: the target's low 16 bits, and a bit map of the
// targets in plane 2.
extern const uint64_t muniDecompositionSingleBits[];
extern const uint16_t muniDecompositionSingleRanks[];
extern const uint16_t muniDecompositionSingles[];
extern const uint8_t muniDecompositionPlaneTwo[];
// Compatibility mappings: sequences in the pool, each its length then its
// code points in UTF-16; Offsets gives where each block's first starts.
uint8_t muniLookupCompatibilityBlock(uint32_t codePoint);
extern const uint64_t muniCompatibilityBlockHash;
extern const uint16_t muniCompatibilityStarts[];
extern const uint16_t muniCompatibilityBlockCount;
extern const uint64_t muniCompatibilityBits[];
extern const uint16_t muniCompatibilityRanks[];
extern const uint16_t muniCompatibilityOffsets[];
extern const uint16_t muniCompatibilityPool[];

// Case data. The Case table stores a record index; a record holds the
// indexes of the upper, lower, title and fold distances in
// muniCaseDeltas, then flags. Special code points, sorted, pack their
// code point << 8 with the lengths of their upper, lower, title and fold
// full mappings in two bits each, from the lowest; the mappings follow
// each other in UTF-16 in the pool from the code point's offset.
uint8_t muniLookupCase(uint32_t codePoint);
extern const uint64_t muniCaseHash;
extern const int32_t muniCaseDeltas[];
extern const uint8_t muniCaseRecords[][5];
extern const uint32_t muniCaseSpecials[];
extern const uint16_t muniCaseSpecialOffsets[];
extern const uint16_t muniCaseSpecialPool[];
extern const uint16_t muniCaseSpecialCount;
// Soft_Dotted, as sorted ranges: the first code point in the low 21
// bits, the number of code points after it above them.
extern const uint32_t muniSoftDotted[];
extern const uint16_t muniSoftDottedCount;

// Security data (UTS #39). The Security table stores 1 for
// Identifier_Status=Allowed.
// Confusable prototypes sit in a rank index whose blocks are found by
// binary search over Starts. An entry below 0x8000 is a prototype of one
// code point; above, it holds the prototype's length in bits 13 and 14
// and its offset in the pool of UTF-16 units in the low 13, and a length
// of 0 means the pool gives the length first.
uint8_t muniLookupSecurity(uint32_t codePoint);
extern const uint64_t muniSecurityHash;
extern const uint16_t muniConfusableStarts[];
extern const uint64_t muniConfusableBits[];
extern const uint16_t muniConfusableRanks[];
extern const uint16_t muniConfusables[];
extern const uint16_t muniConfusablePool[];
extern const uint16_t muniConfusableBlockCount;

enum
{
    muni_caseFlagCased = 1,
    muni_caseFlagIgnorable = 2,
    muni_caseFlagSpecial = 4,
};

#endif // MAUL_UNICODE_SRC_TABLES_H
