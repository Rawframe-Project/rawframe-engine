// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fonts of a text service (record mui-0006): TrueType and OpenType fonts
// and collections, from memory. Font files are hostile input: a font is
// validated when it is created, and compressed web fonts (WOFF, WOFF2)
// are refused.

#ifndef MAUL_UI_FONT_H
#define MAUL_UI_FONT_H

#include "maul-ui/base.h"
#include "maul-ui/text.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A font, in the shape of every id (family record 0016).
    typedef struct muiFontId
    {
        uint32_t index1;
        uint32_t generation;
    } muiFontId;

    // Who keeps a font's bytes.
    typedef uint8_t muiFontData;

    enum
    {
        // The service copies them, and the caller's may go at once.
        mui_fontDataCopy = 0,
        // The service reads the caller's, which stay valid and unchanged
        // until the font is destroyed: no copy of a large font.
        mui_fontDataBorrow = 1,
    };

    // How a font is made. Build it with muiDefaultFontDef.
    typedef struct muiFontDef
    {
        uint32_t cookie;
        // The font file's bytes.
        const void* data;
        size_t size;
        // The face of a collection, from 0; 0 for a single font.
        uint32_t faceIndex;
        muiFontData dataMode;
    } muiFontDef;

    // A font's metrics. Distances are in ems from the baseline: times a
    // font size, they are logical units. The ascent is above the
    // baseline and the descent below it, both positive for usual fonts.
    typedef struct muiFontMetrics
    {
        // Design units per em, from 16 to 16,384.
        uint32_t unitsPerEm;
        uint32_t glyphCount;
        // The typographic ascent, descent and line gap when the font asks
        // for them (OS/2 USE_TYPO_METRICS), else the horizontal header's,
        // else the typographic or Windows ones, as HarfBuzz and the
        // OpenType recommendations choose.
        float ascent;
        float descent;
        float lineGap;
        // The heights of capitals and of x, 0 when the font gives none.
        float capHeight;
        float xHeight;
        // The top of the underline below the baseline, and its thickness.
        float underlineOffset;
        float underlineThickness;
        // The top of the strikeout above the baseline, and its thickness.
        float strikeoutOffset;
        float strikeoutThickness;
    } muiFontMetrics;

    /// Returns the default font def: no data, face 0, copied.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiFontDef muiDefaultFontDef(void);

    /// Counts the faces of a font file without reading them: 1 for a
    /// single font, the number a collection declares for a collection.
    /// Allocates nothing.
    ///
    /// @param data        The file's bytes.
    /// @param size        Their count.
    /// @param countOut    Receives the count; 0 on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument;
    ///         `mui_errorFormat` for data that is not a TrueType or
    ///         OpenType font or collection, or a collection whose header
    ///         does not fit in size.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_NODISCARD MUI_API muiResult muiCountFontFaces(const void* data, size_t size,
                                                      uint32_t* countOut);

    /// Creates a font from a face of a font file, validating it.
    ///
    /// @param service  The service.
    /// @param def      The font: a valid cookie, data and a size from 12 to
    ///                 2^31 - 1, and a data mode above.
    /// @param fontOut  Receives the font; the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad
    ///         cookie, a size out of range or an unknown data mode;
    ///         `mui_errorFormat` for data that is not a font the service
    ///         reads, a face index past the file's faces, or a font that
    ///         fails validation; `mui_errorCapacity` past the font limit or
    ///         when memory runs out.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateFont(muiTextService* service, const muiFontDef* def,
                                                  muiFontId* fontOut);

    /// Destroys a font, which releases a copy of its bytes or ends the
    /// borrow of the caller's.
    ///
    /// @param service  The service.
    /// @param fontId   The font.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service or the
    ///         null id; `mui_errorStale` for a font that is gone.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyFont(muiTextService* service, muiFontId fontId);

    /// Tells whether an id names a font of the service that still exists.
    ///
    /// @param service  The service, or NULL.
    /// @param fontId   The id.
    /// @return true for a live font; false otherwise.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_API bool muiFont_IsValid(const muiTextService* service, muiFontId fontId);

    /// Reads a font's metrics.
    ///
    /// @param service     The service.
    /// @param fontId      The font.
    /// @param metricsOut  Receives the metrics; unchanged on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for a font that is gone.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiFont_GetMetrics(const muiTextService* service,
                                                       muiFontId fontId,
                                                       muiFontMetrics* metricsOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_FONT_H
