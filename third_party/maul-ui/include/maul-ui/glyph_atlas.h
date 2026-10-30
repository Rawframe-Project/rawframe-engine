// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph atlases: glyph images of a text service's fonts, rendered as
// muiTextService_RenderGlyph, muiTextService_RenderGlyphField and
// muiTextService_RenderGlyphMultiField render them and packed into pages a renderer uploads as
// textures. An atlas's pages are of one format: one byte a pixel, holding coverage and distance
// fields both, four holding multi-channel fields, or four holding colour
// glyphs; a renderer wanting several makes an atlas of each, so their
// pages never share an index space. Pages are split into plots; when no
// plot has room, the least recently used plot that the current frame has
// not used is emptied and packed again. The atlas keeps the pages' pixels
// and tells which rectangles changed; it uses no graphics API.

#ifndef MAUL_UI_GLYPH_ATLAS_H
#define MAUL_UI_GLYPH_ATLAS_H

#include "maul-ui/base.h"
#include "maul-ui/draw.h"
#include "maul-ui/text.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // An atlas, an owner object of its own, used by one thread at a time
    // with its text service.
    typedef struct muiGlyphAtlas muiGlyphAtlas;

    // The pixels of an atlas's pages.
    typedef uint8_t muiAtlasFormat;

    enum
    {
        // A byte a pixel: coverage (muiGlyphAtlas_GetGlyph) and distance fields
        // (muiGlyphAtlas_GetGlyphField).
        mui_atlasOneChannel = 0,
        // Four bytes a pixel, red, green, blue and alpha: multi-channel
        // distance fields (muiGlyphAtlas_GetGlyphMultiField).
        mui_atlasFourChannel = 1,
        // Four bytes a pixel, premultiplied colour as an sRGB texture holds
        // it: colour glyphs (muiGlyphAtlas_GetColorGlyph).
        mui_atlasColor = 2,
    };

    enum
    {
        // A page's sides, in pixels.
        MUI_MIN_ATLAS_PAGE = 64,
        MUI_MAX_ATLAS_PAGE = 16384,
        // A plot's sides, in pixels.
        MUI_MIN_ATLAS_PLOT = 16,
        MUI_MAX_ATLAS_PLOT = 4096,
        // The most plots a page is cut into, and pages an atlas has.
        MUI_MAX_ATLAS_PLOTS = 4096,
        MUI_MAX_ATLAS_PAGES = 64
    };

    // The farthest a pen is from 0, in device pixels, so pixel positions
    // stay exact.
#define MUI_MAX_ATLAS_PEN 16777216.0f

    // How an atlas is made. Build it with muiDefaultGlyphAtlasDef. Pages
    // are pageWidth by pageHeight pixels, cut into plots plotWidth by
    // plotHeight, which divide them; a glyph image larger than a plot,
    // less a pixel of gutter on each side, is not packed.
    typedef struct muiGlyphAtlasDef
    {
        uint32_t cookie;
        uint32_t pageWidth;
        uint32_t pageHeight;
        uint32_t plotWidth;
        uint32_t plotHeight;
        // How many pages there may be, made as they are needed.
        uint32_t maxPages;
        // The pages' pixels.
        muiAtlasFormat format;
    } muiGlyphAtlasDef;

    // A glyph's image in an atlas: its page, its top left there (u, v)
    // and its size, and where its top left goes in pixels (x, y, down):
    // for coverage, in device pixels for the pen and baseline it was asked
    // for; for a field, from the pen and baseline at the field's size. An
    // empty image has no size and nothing to draw.
    typedef struct muiAtlasGlyph
    {
        uint32_t page;
        uint32_t u;
        uint32_t v;
        uint32_t width;
        uint32_t height;
        int32_t x;
        int32_t y;
    } muiAtlasGlyph;

    // A page's pixels: rows of width pixels, from the top, each pixel of
    // as many bytes as its format has channels.
    typedef struct muiAtlasPage
    {
        const unsigned char* pixels;
        uint32_t width;
        uint32_t height;
        muiAtlasFormat format;
    } muiAtlasPage;

    // A rectangle of a page whose pixels changed since the renderer last
    // took the changes.
    typedef struct muiAtlasUpdate
    {
        uint32_t page;
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    } muiAtlasUpdate;

    /// Returns the default atlas def: pages of 1,024 by 1,024 in plots of
    /// 256 by 256, at most 4 pages, a byte a pixel.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiGlyphAtlasDef muiDefaultGlyphAtlasDef(void);

    /// Creates an atlas of a text service's glyphs, in the service's
    /// memory. It is destroyed before the service.
    ///
    /// @param service   The service.
    /// @param def       The atlas: a valid cookie; pages of
    ///                  MUI_MIN_ATLAS_PAGE to MUI_MAX_ATLAS_PAGE pixels a
    ///                  side; plots of MUI_MIN_ATLAS_PLOT to
    ///                  MUI_MAX_ATLAS_PLOT pixels a side that divide the
    ///                  pages, at most MUI_MAX_ATLAS_PLOTS of them a page;
    ///                  from 1 to MUI_MAX_ATLAS_PAGES pages; a format of
    ///                  muiAtlasFormat.
    /// @param atlasOut  Receives the atlas. NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad
    ///         cookie, plots that do not divide the pages, no pages or an
    ///         unknown format; `mui_errorLimit` for sides, plots or pages
    ///         past the bounds above;
    ///         `mui_errorCapacity` when memory runs out; `mui_errorBusy` while
    ///         the service is busy.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateGlyphAtlas(muiTextService* service,
                                                        const muiGlyphAtlasDef* def,
                                                        muiGlyphAtlas** atlasOut);

    /// Destroys an atlas and its pages. A call while the atlas is busy
    /// destroys nothing and counts one misuse on its service.
    ///
    /// @param atlas  The atlas, or NULL for nothing.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_API void muiDestroyGlyphAtlas(muiGlyphAtlas* atlas);

    /// Starts a frame: glyphs got before it may be evicted to make room. A
    /// call while the atlas is busy does nothing and counts one misuse on
    /// its service.
    ///
    /// @param atlas  The atlas, or NULL for nothing.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_API void muiGlyphAtlas_BeginFrame(muiGlyphAtlas* atlas);

    /// Gets a glyph's image for a pen in device pixels, rendering and
    /// packing it the first time. The pen's x is taken to the nearest
    /// quarter pixel and its baseline to the nearest pixel; the font key
    /// 0 is the default font's. The glyph's plot is kept until a later
    /// frame.
    ///
    /// @param atlas      The atlas.
    /// @param font       A font key, as a glyph run carries.
    /// @param glyph      A glyph id of the font.
    /// @param pixelSize  The em in device pixels, as muiTextService_RenderGlyph takes.
    /// @param penX       The pen, in device pixels, within MUI_MAX_ATLAS_PEN
    ///                   of 0.
    /// @param baselineY  The baseline, likewise.
    /// @param glyphOut   Receives the image; when the image is larger than a
    ///                   plot, its width and height only. Zeroed on any other
    ///                   failure.
    /// @return `mui_success`; `mui_errorCapacity` when the image is larger than
    ///         a plot, every plot is in use this frame, or memory runs out;
    ///         `mui_errorInvalid` for a NULL atlas or glyphOut, a size that is
    ///         not a finite number above 0, a position that is not finite, or
    ///         a glyph id the font lacks; `mui_errorLimit` for a size outside
    ///         MUI_MIN_GLYPH_PIXEL_SIZE to MUI_MAX_GLYPH_PIXEL_SIZE, or a
    ///         position past MUI_MAX_ATLAS_PEN;
    ///         `mui_errorUnsupported` for an atlas of another format;
    ///         `mui_errorStale` for a key that names no font; `mui_errorFormat`
    ///         for a glyph that cannot be rendered; `mui_errorBusy` while the
    ///         atlas is busy.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_GetGlyph(muiGlyphAtlas* atlas, uint64_t font,
                                                           uint32_t glyph, float pixelSize,
                                                           float penX, float baselineY,
                                                           muiAtlasGlyph* glyphOut);

    /// Gets a colour glyph's image from an atlas of colour glyphs for a pen
    /// in device pixels, rendering and packing it the first time, as
    /// muiTextService_RenderColorGlyph renders it and muiGlyphAtlas_GetGlyph places coverage.
    /// The text's colour is kept as 8-bit sRGB with straight alpha, and
    /// the glyph is rendered with it so kept: each palette and text colour
    /// is an image of its own. A glyph without colour layers gives
    /// `mui_empty`, to be drawn as coverage; a font without a COLR table
    /// gives it without looking.
    ///
    /// @param atlas       The atlas, of colour glyphs.
    /// @param font        A font key, as a glyph run carries.
    /// @param glyph       A glyph id of the font.
    /// @param pixelSize   The em in device pixels.
    /// @param penX        The pen, in device pixels, within MUI_MAX_ATLAS_PEN
    ///                    of 0.
    /// @param baselineY   The baseline, likewise.
    /// @param palette     The font's palette to fill from.
    /// @param foreground  The text's colour, linear and premultiplied.
    /// @param glyphOut    Receives the image; when the image is larger than a
    ///                    plot, its width and height only. Zeroed on any other
    ///                    failure.
    /// @return As muiGlyphAtlas_GetGlyph, with `mui_empty` for a glyph without
    ///         colour layers;
    ///         `mui_errorBusy` while the atlas is busy.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_GetColorGlyph(
        muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph, float pixelSize, float penX,
        float baselineY, uint32_t palette, muiLinearColor foreground, muiAtlasGlyph* glyphOut);

    /// Gets a glyph's distance field, rendering and packing it the first
    /// time, as muiTextService_RenderGlyphField renders it; the font key 0 is the
    /// default font's. A renderer draws it at any size s by scaling the
    /// image and its place, x and y from the pen and baseline, by
    /// s / pixelSize. The glyph's plot is kept until a later frame.
    ///
    /// @param atlas      The atlas.
    /// @param font       A font key, as a glyph run carries.
    /// @param glyph      A glyph id of the font.
    /// @param pixelSize  The em in pixels of the field, as
    ///                   muiTextService_RenderGlyphField takes.
    /// @param spread     How far the field reaches past the outline, from
    ///                   MUI_MIN_FIELD_SPREAD to MUI_MAX_FIELD_SPREAD
    ///                   pixels.
    /// @param glyphOut   Receives the image; when the image is larger than a
    ///                   plot, its width and height only. Zeroed on any other
    ///                   failure.
    /// @return As muiGlyphAtlas_GetGlyph, with `mui_errorLimit` for a spread out of
    ///         range;
    ///         `mui_errorBusy` while the atlas is busy.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_GetGlyphField(muiGlyphAtlas* atlas, uint64_t font,
                                                                uint32_t glyph, float pixelSize,
                                                                uint32_t spread,
                                                                muiAtlasGlyph* glyphOut);

    /// Gets a glyph's multi-channel distance field from an atlas of four
    /// channels, rendering and packing it the first time, as
    /// muiTextService_RenderGlyphMultiField renders it; the font key 0 is the default
    /// font's. It is placed and drawn as muiGlyphAtlas_GetGlyphField's fields
    /// are, from the median of its red, green and blue, or from its alpha
    /// as a one-channel field. The glyph's plot is kept until a later
    /// frame.
    ///
    /// @param atlas      The atlas, of four channels.
    /// @param font       A font key, as a glyph run carries.
    /// @param glyph      A glyph id of the font.
    /// @param pixelSize  The em in pixels of the field, as
    ///                   muiTextService_RenderGlyphMultiField takes.
    /// @param spread     How far the field reaches past the outline, from
    ///                   MUI_MIN_FIELD_SPREAD to MUI_MAX_FIELD_SPREAD
    ///                   pixels.
    /// @param glyphOut   Receives the image; when the image is larger than a
    ///                   plot, its width and height only. Zeroed on any other
    ///                   failure.
    /// @return As muiGlyphAtlas_GetGlyphField; `mui_errorBusy` while the atlas is
    ///         busy.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_GetGlyphMultiField(muiGlyphAtlas* atlas,
                                                                     uint64_t font, uint32_t glyph,
                                                                     float pixelSize,
                                                                     uint32_t spread,
                                                                     muiAtlasGlyph* glyphOut);

    /// Returns how many pages the atlas has made.
    ///
    /// @param atlas  The atlas, or NULL for 0.
    /// @return The page count; pages are numbered from 0.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_API uint32_t muiGlyphAtlas_GetPageCount(const muiGlyphAtlas* atlas);

    /// Reads a page's pixels, which stay valid until the atlas is
    /// destroyed and change only in calls to muiGlyphAtlas_GetGlyph.
    ///
    /// @param atlas    The atlas.
    /// @param page     A page the atlas has made.
    /// @param pageOut  Receives the page. Zeroed on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         page not made.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_GetPage(const muiGlyphAtlas* atlas, uint32_t page,
                                                          muiAtlasPage* pageOut);

    /// Takes the rectangles of pages that changed since the last call, a
    /// plot's changes as one rectangle, for the renderer to copy from the
    /// pages' pixels.
    ///
    /// @param atlas       The atlas.
    /// @param updatesOut  Receives the rectangles; may be NULL when capacity
    ///                    is 0.
    /// @param capacity    How many updatesOut holds.
    /// @param countOut    Receives how many rectangles there are, also when
    ///                    updatesOut is too small. 0 on any other failure.
    /// @return `mui_success`, taking them; `mui_errorCapacity` when updatesOut
    ///         holds fewer, taking none; `mui_errorInvalid` for a NULL atlas or
    ///         countOut, or NULL updatesOut with a capacity; `mui_errorBusy`
    ///         while the atlas is busy.
    /// @par Thread safety
    /// Safe from any thread; the atlas is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_TakeUpdates(muiGlyphAtlas* atlas,
                                                              muiAtlasUpdate* updatesOut,
                                                              uint32_t capacity,
                                                              uint32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_GLYPH_ATLAS_H
