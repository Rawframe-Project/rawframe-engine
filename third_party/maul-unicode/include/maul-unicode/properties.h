// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Character properties from the Unicode Character Database. Every lookup
// takes a code point, cannot fail and returns the property's value; a
// value above U+10FFFF gets the value of an unassigned code point.

#ifndef MAUL_UNICODE_PROPERTIES_H
#define MAUL_UNICODE_PROPERTIES_H

#include "maul-unicode/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // General_Category, one value per UCD short name. The numbering is the
    // library's own and stable within a major version.
    typedef uint8_t muniGeneralCategory;

    enum
    {
        muni_gcCn = 0,  // Unassigned
        muni_gcLu = 1,  // Uppercase_Letter
        muni_gcLl = 2,  // Lowercase_Letter
        muni_gcLt = 3,  // Titlecase_Letter
        muni_gcLm = 4,  // Modifier_Letter
        muni_gcLo = 5,  // Other_Letter
        muni_gcMn = 6,  // Nonspacing_Mark
        muni_gcMc = 7,  // Spacing_Mark
        muni_gcMe = 8,  // Enclosing_Mark
        muni_gcNd = 9,  // Decimal_Number
        muni_gcNl = 10, // Letter_Number
        muni_gcNo = 11, // Other_Number
        muni_gcPc = 12, // Connector_Punctuation
        muni_gcPd = 13, // Dash_Punctuation
        muni_gcPs = 14, // Open_Punctuation
        muni_gcPe = 15, // Close_Punctuation
        muni_gcPi = 16, // Initial_Punctuation
        muni_gcPf = 17, // Final_Punctuation
        muni_gcPo = 18, // Other_Punctuation
        muni_gcSm = 19, // Math_Symbol
        muni_gcSc = 20, // Currency_Symbol
        muni_gcSk = 21, // Modifier_Symbol
        muni_gcSo = 22, // Other_Symbol
        muni_gcZs = 23, // Space_Separator
        muni_gcZl = 24, // Line_Separator
        muni_gcZp = 25, // Paragraph_Separator
        muni_gcCc = 26, // Control
        muni_gcCf = 27, // Format
        muni_gcCs = 28, // Surrogate
        muni_gcCo = 29, // Private_Use
    };

    /// Returns the General_Category of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is unassigned.
    /// @return The category, one of the muni_gc values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniGeneralCategory muniGetGeneralCategory(uint32_t codePoint);

    // Grapheme_Cluster_Break (UAX #29), in the library's numbering.
    typedef uint8_t muniGraphemeBreak;

    enum
    {
        muni_gcbOther = 0,
        muni_gcbCr = 1,
        muni_gcbLf = 2,
        muni_gcbControl = 3,
        muni_gcbExtend = 4,
        muni_gcbZwj = 5,
        muni_gcbRegionalIndicator = 6,
        muni_gcbPrepend = 7,
        muni_gcbSpacingMark = 8,
        muni_gcbL = 9,
        muni_gcbV = 10,
        muni_gcbT = 11,
        muni_gcbLv = 12,
        muni_gcbLvt = 13,
    };

    // Word_Break (UAX #29), in the library's numbering.
    typedef uint8_t muniWordBreak;

    enum
    {
        muni_wbOther = 0,
        muni_wbCr = 1,
        muni_wbLf = 2,
        muni_wbNewline = 3,
        muni_wbExtend = 4,
        muni_wbZwj = 5,
        muni_wbRegionalIndicator = 6,
        muni_wbFormat = 7,
        muni_wbKatakana = 8,
        muni_wbHebrewLetter = 9,
        muni_wbALetter = 10,
        muni_wbSingleQuote = 11,
        muni_wbDoubleQuote = 12,
        muni_wbMidNumLet = 13,
        muni_wbMidLetter = 14,
        muni_wbMidNum = 15,
        muni_wbNumeric = 16,
        muni_wbExtendNumLet = 17,
        muni_wbWSegSpace = 18,
    };

    // Sentence_Break (UAX #29), in the library's numbering.
    typedef uint8_t muniSentenceBreak;

    enum
    {
        muni_sbOther = 0,
        muni_sbCr = 1,
        muni_sbLf = 2,
        muni_sbExtend = 3,
        muni_sbSep = 4,
        muni_sbFormat = 5,
        muni_sbSp = 6,
        muni_sbLower = 7,
        muni_sbUpper = 8,
        muni_sbOLetter = 9,
        muni_sbNumeric = 10,
        muni_sbATerm = 11,
        muni_sbSContinue = 12,
        muni_sbSTerm = 13,
        muni_sbClose = 14,
    };

    // Indic_Conjunct_Break, which grapheme cluster rule GB9c reads.
    typedef uint8_t muniIndicConjunctBreak;

    enum
    {
        muni_incbNone = 0,
        muni_incbLinker = 1,
        muni_incbConsonant = 2,
        muni_incbExtend = 3,
    };

    /// Returns the Grapheme_Cluster_Break of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is Other.
    /// @return The value, one of the muni_gcb values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniGraphemeBreak muniGetGraphemeBreak(uint32_t codePoint);

    /// Returns the Word_Break of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is Other.
    /// @return The value, one of the muni_wb values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniWordBreak muniGetWordBreak(uint32_t codePoint);

    /// Returns the Sentence_Break of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is Other.
    /// @return The value, one of the muni_sb values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniSentenceBreak muniGetSentenceBreak(uint32_t codePoint);

    /// Returns the Indic_Conjunct_Break of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is None.
    /// @return The value, one of the muni_incb values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniIndicConjunctBreak muniGetIndicConjunctBreak(uint32_t codePoint);

    /// Tells whether a code point has the Extended_Pictographic property
    /// (UTS #51), which keeps emoji sequences together.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has it not.
    /// @return true when the code point is Extended_Pictographic.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsExtendedPictographic(uint32_t codePoint);

    /// Tells whether a code point has the Emoji property (UTS #51): it
    /// can show as an emoji, which digits and '#' can too.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has it not.
    /// @return true when the code point is Emoji.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsEmoji(uint32_t codePoint);

    /// Tells whether a code point has the Emoji_Presentation property
    /// (UTS #51): it shows as an emoji unless a variation selector asks
    /// for text.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has it not.
    /// @return true when the code point is Emoji_Presentation.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsEmojiPresentation(uint32_t codePoint);

    /// Tells whether a code point has the Emoji_Modifier property (UTS
    /// #51): a skin tone modifier.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has it not.
    /// @return true when the code point is Emoji_Modifier.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsEmojiModifier(uint32_t codePoint);

    /// Tells whether a code point has the Emoji_Modifier_Base property
    /// (UTS #51): a skin tone modifier after it applies to it.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has it not.
    /// @return true when the code point is Emoji_Modifier_Base.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsEmojiModifierBase(uint32_t codePoint);

    /// Tells whether a code point has the Emoji_Component property (UTS
    /// #51): it can be part of an emoji sequence, as regional indicators,
    /// keycap parts, tags and the zero width joiner are.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has it not.
    /// @return true when the code point is Emoji_Component.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsEmojiComponent(uint32_t codePoint);

    /// Tells whether a code point has the White_Space property: spaces,
    /// tabs and line and paragraph separators, 25 code points in all.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has it not.
    /// @return true when the code point is White_Space.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsWhiteSpace(uint32_t codePoint);

    /// Tells whether a code point is Default_Ignorable_Code_Point: a
    /// character that shows nothing when a font lacks it, such as a
    /// zero width joiner or a variation selector.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has it not.
    /// @return true for a default ignorable code point.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsDefaultIgnorable(uint32_t codePoint);

    /// Returns the value of a decimal digit: a code point of General
    /// Category Nd (Numeric_Type Decimal), such as '7' or the Devanagari
    /// seven, which parse as numbers. Superscripts, fractions and Roman
    /// numerals have numeric values but are not decimal digits.
    ///
    /// @param codePoint  Any value.
    /// @return The digit's value, 0 to 9, or -1 when the code point is
    ///         not a decimal digit.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API int32_t muniGetDecimalDigitValue(uint32_t codePoint);

    // Line_Break (UAX #14), in the library's numbering; the comments give
    // the long names.
    typedef uint8_t muniLineBreak;

    enum
    {
        muni_lbXx = 0,   // Unknown
        muni_lbBk = 1,   // Mandatory_Break
        muni_lbCr = 2,   // Carriage_Return
        muni_lbLf = 3,   // Line_Feed
        muni_lbCm = 4,   // Combining_Mark
        muni_lbNl = 5,   // Next_Line
        muni_lbSg = 6,   // Surrogate
        muni_lbWj = 7,   // Word_Joiner
        muni_lbZw = 8,   // ZWSpace
        muni_lbGl = 9,   // Glue
        muni_lbSp = 10,  // Space
        muni_lbZwj = 11, // ZWJ
        muni_lbB2 = 12,  // Break_Both
        muni_lbBa = 13,  // Break_After
        muni_lbBb = 14,  // Break_Before
        muni_lbHy = 15,  // Hyphen
        muni_lbCb = 16,  // Contingent_Break
        muni_lbCl = 17,  // Close_Punctuation
        muni_lbCp = 18,  // Close_Parenthesis
        muni_lbEx = 19,  // Exclamation
        muni_lbIn = 20,  // Inseparable
        muni_lbNs = 21,  // Nonstarter
        muni_lbOp = 22,  // Open_Punctuation
        muni_lbQu = 23,  // Quotation
        muni_lbIs = 24,  // Infix_Numeric
        muni_lbNu = 25,  // Numeric
        muni_lbPo = 26,  // Postfix_Numeric
        muni_lbPr = 27,  // Prefix_Numeric
        muni_lbSy = 28,  // Break_Symbols
        muni_lbAi = 29,  // Ambiguous
        muni_lbAl = 30,  // Alphabetic
        muni_lbCj = 31,  // Conditional_Japanese_Starter
        muni_lbEb = 32,  // E_Base
        muni_lbEm = 33,  // E_Modifier
        muni_lbH2 = 34,  // H2
        muni_lbH3 = 35,  // H3
        muni_lbHl = 36,  // Hebrew_Letter
        muni_lbId = 37,  // Ideographic
        muni_lbJl = 38,  // JL
        muni_lbJv = 39,  // JV
        muni_lbJt = 40,  // JT
        muni_lbRi = 41,  // Regional_Indicator
        muni_lbSa = 42,  // Complex_Context
        muni_lbAk = 43,  // Aksara
        muni_lbAp = 44,  // Aksara_Prebase
        muni_lbAs = 45,  // Aksara_Start
        muni_lbVf = 46,  // Virama_Final
        muni_lbVi = 47,  // Virama
        muni_lbHh = 48,  // Unambiguous_Hyphen
    };

    // East_Asian_Width (UAX #11), in the library's numbering.
    typedef uint8_t muniEastAsianWidth;

    enum
    {
        muni_eawNeutral = 0,
        muni_eawAmbiguous = 1,
        muni_eawHalfwidth = 2,
        muni_eawWide = 3,
        muni_eawFullwidth = 4,
        muni_eawNarrow = 5,
    };

    // Bidi_Class (UAX #9), in the library's numbering; the comments give
    // the long names.
    typedef uint8_t muniBidiClass;

    enum
    {
        muni_bcL = 0,    // Left_To_Right
        muni_bcR = 1,    // Right_To_Left
        muni_bcAl = 2,   // Arabic_Letter
        muni_bcEn = 3,   // European_Number
        muni_bcEs = 4,   // European_Separator
        muni_bcEt = 5,   // European_Terminator
        muni_bcAn = 6,   // Arabic_Number
        muni_bcCs = 7,   // Common_Separator
        muni_bcNsm = 8,  // Nonspacing_Mark
        muni_bcBn = 9,   // Boundary_Neutral
        muni_bcB = 10,   // Paragraph_Separator
        muni_bcS = 11,   // Segment_Separator
        muni_bcWs = 12,  // White_Space
        muni_bcOn = 13,  // Other_Neutral
        muni_bcLre = 14, // Left_To_Right_Embedding
        muni_bcLro = 15, // Left_To_Right_Override
        muni_bcRle = 16, // Right_To_Left_Embedding
        muni_bcRlo = 17, // Right_To_Left_Override
        muni_bcPdf = 18, // Pop_Directional_Format
        muni_bcLri = 19, // Left_To_Right_Isolate
        muni_bcRli = 20, // Right_To_Left_Isolate
        muni_bcFsi = 21, // First_Strong_Isolate
        muni_bcPdi = 22, // Pop_Directional_Isolate
    };

    // Bidi_Paired_Bracket_Type (UAX #9): whether a code point opens or
    // closes a bracket pair the bidi algorithm matches.
    typedef uint8_t muniBracketType;

    enum
    {
        muni_bracketNone = 0,
        muni_bracketOpen = 1,
        muni_bracketClose = 2,
    };

    // A script as its ISO 15924 tag: four ASCII letters, big-endian, so
    // MUNI_SCRIPT('L', 'a', 't', 'n') is Latin. The same encoding as
    // HarfBuzz's hb_script_t.
    typedef uint32_t muniScript;

#define MUNI_SCRIPT(a, b, c, d)                                                                    \
    ((muniScript)(((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) |                \
                  ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d)))
// Common, for characters used by several scripts.
#define MUNI_SCRIPT_COMMON MUNI_SCRIPT('Z', 'y', 'y', 'y')
// Inherited, for marks that take the script of their base.
#define MUNI_SCRIPT_INHERITED MUNI_SCRIPT('Z', 'i', 'n', 'h')
// Unknown, for unassigned code points.
#define MUNI_SCRIPT_UNKNOWN MUNI_SCRIPT('Z', 'z', 'z', 'z')

    /// Returns the Line_Break class of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is Unknown (XX).
    /// @return The class, one of the muni_lb values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniLineBreak muniGetLineBreak(uint32_t codePoint);

    /// Returns the East_Asian_Width of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is Neutral.
    /// @return The width, one of the muni_eaw values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniEastAsianWidth muniGetEastAsianWidth(uint32_t codePoint);

    /// Returns the Bidi_Class of a code point, with the UCD's defaults for
    /// unassigned code points in right-to-left blocks.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is Left_To_Right.
    /// @return The class, one of the muni_bc values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniBidiClass muniGetBidiClass(uint32_t codePoint);

    /// Returns the Bidi_Mirroring_Glyph of a code point: the character
    /// whose glyph is its mirror image, such as ')' for '(', which right-
    /// to-left text displays in its place.
    ///
    /// @param codePoint  Any value.
    /// @return The mirror, or codePoint itself when it has none.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API uint32_t muniGetMirroringGlyph(uint32_t codePoint);

    /// Returns the Bidi_Paired_Bracket_Type of a code point. The bracket
    /// it pairs with is its muniGetMirroringGlyph.
    ///
    /// @param codePoint  Any value.
    /// @return One of the muni_bracket values.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniBracketType muniGetBracketType(uint32_t codePoint);

    /// Returns the Canonical_Combining_Class of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF has class 0.
    /// @return The class, from 0 (not reordered) to 254.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API uint8_t muniGetCombiningClass(uint32_t codePoint);

    /// Returns the Script of a code point.
    ///
    /// @param codePoint  Any value; one above U+10FFFF is Unknown.
    /// @return The script's ISO 15924 tag, as MUNI_SCRIPT builds it.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniScript muniGetScript(uint32_t codePoint);

    /// Writes the Script_Extensions of a code point (UAX #24): the scripts
    /// it is used with. A code point without listed extensions has its
    /// Script alone, so there is always at least one.
    ///
    /// @param codePoint  Any value.
    /// @param scripts    The output. May be NULL when capacity is 0.
    /// @param capacity   The number of scripts the output can hold; 32
    ///                   always suffice.
    /// @param countOut   Receives the number of scripts.
    /// @return `muni_success`, `muni_errorCapacity` when they do not all
    ///         fit (the ones that fit are written), or `muni_errorInvalid`
    ///         for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniGetScriptExtensions(uint32_t codePoint,
                                                               muniScript* scripts, size_t capacity,
                                                               size_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_PROPERTIES_H
