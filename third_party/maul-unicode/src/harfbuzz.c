// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// HarfBuzz Unicode functions over Maul Unicode's public API. HarfBuzz
// numbers the general categories in its own order and spells scripts as
// ISO 15924 tags, as Maul Unicode does.

#include "maul-unicode/harfbuzz.h"

#include "maul-unicode/normalize.h"
#include "maul-unicode/properties.h"

#include <hb.h>

// HarfBuzz's value for each muni_gc value, in muni_gc order.
static const hb_unicode_general_category_t s_categories[] = {
    HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED,          // Cn
    HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER,    // Lu
    HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER,    // Ll
    HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER,    // Lt
    HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER,     // Lm
    HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER,        // Lo
    HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK,    // Mn
    HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK,        // Mc
    HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK,      // Me
    HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER,      // Nd
    HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER,       // Nl
    HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER,        // No
    HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION, // Pc
    HB_UNICODE_GENERAL_CATEGORY_DASH_PUNCTUATION,    // Pd
    HB_UNICODE_GENERAL_CATEGORY_OPEN_PUNCTUATION,    // Ps
    HB_UNICODE_GENERAL_CATEGORY_CLOSE_PUNCTUATION,   // Pe
    HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION, // Pi
    HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION,   // Pf
    HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION,   // Po
    HB_UNICODE_GENERAL_CATEGORY_MATH_SYMBOL,         // Sm
    HB_UNICODE_GENERAL_CATEGORY_CURRENCY_SYMBOL,     // Sc
    HB_UNICODE_GENERAL_CATEGORY_MODIFIER_SYMBOL,     // Sk
    HB_UNICODE_GENERAL_CATEGORY_OTHER_SYMBOL,        // So
    HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR,     // Zs
    HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR,      // Zl
    HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR, // Zp
    HB_UNICODE_GENERAL_CATEGORY_CONTROL,             // Cc
    HB_UNICODE_GENERAL_CATEGORY_FORMAT,              // Cf
    HB_UNICODE_GENERAL_CATEGORY_SURROGATE,           // Cs
    HB_UNICODE_GENERAL_CATEGORY_PRIVATE_USE,         // Co
};

static_assert(sizeof(s_categories) / sizeof(s_categories[0]) == muni_gcCo + 1,
              "one HarfBuzz category per muni_gc value");

static hb_unicode_combining_class_t CombiningClass(hb_unicode_funcs_t* functions,
                                                   hb_codepoint_t codePoint, void* context)
{
    (void)functions;
    (void)context;
    return (hb_unicode_combining_class_t)muniGetCombiningClass(codePoint);
}

static hb_unicode_general_category_t GeneralCategory(hb_unicode_funcs_t* functions,
                                                     hb_codepoint_t codePoint, void* context)
{
    (void)functions;
    (void)context;
    return s_categories[muniGetGeneralCategory(codePoint)];
}

static hb_codepoint_t Mirroring(hb_unicode_funcs_t* functions, hb_codepoint_t codePoint,
                                void* context)
{
    (void)functions;
    (void)context;
    return muniGetMirroringGlyph(codePoint);
}

static hb_script_t Script(hb_unicode_funcs_t* functions, hb_codepoint_t codePoint, void* context)
{
    (void)functions;
    (void)context;
    return hb_script_from_iso15924_tag(muniGetScript(codePoint));
}

static hb_bool_t Compose(hb_unicode_funcs_t* functions, hb_codepoint_t first, hb_codepoint_t second,
                         hb_codepoint_t* compositeOut, void* context)
{
    (void)functions;
    (void)context;
    uint32_t composite = muniComposePair(first, second);
    if (composite == 0)
    {
        return false;
    }
    *compositeOut = composite;
    return true;
}

static hb_bool_t Decompose(hb_unicode_funcs_t* functions, hb_codepoint_t codePoint,
                           hb_codepoint_t* firstOut, hb_codepoint_t* secondOut, void* context)
{
    (void)functions;
    (void)context;
    uint32_t first;
    uint32_t second;
    if (!muniDecomposePair(codePoint, &first, &second))
    {
        return false;
    }
    *firstOut = first;
    *secondOut = second;
    return true;
}

hb_unicode_funcs_t* muniCreateHarfBuzzFunctions(void)
{
    hb_unicode_funcs_t* functions = hb_unicode_funcs_create(hb_unicode_funcs_get_empty());
    hb_unicode_funcs_set_combining_class_func(functions, CombiningClass, nullptr, nullptr);
    hb_unicode_funcs_set_general_category_func(functions, GeneralCategory, nullptr, nullptr);
    hb_unicode_funcs_set_mirroring_func(functions, Mirroring, nullptr, nullptr);
    hb_unicode_funcs_set_script_func(functions, Script, nullptr, nullptr);
    hb_unicode_funcs_set_compose_func(functions, Compose, nullptr, nullptr);
    hb_unicode_funcs_set_decompose_func(functions, Decompose, nullptr, nullptr);
    hb_unicode_funcs_make_immutable(functions);
    return functions;
}
