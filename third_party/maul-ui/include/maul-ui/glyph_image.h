// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph images (record mui-0006): a glyph of a text service's font
// rendered for a renderer to draw, as 8-bit coverage. Outlines are
// rendered unhinted, as text is laid out, so an image sits where its
// glyph run places it at every size; images are the same on every
// platform.

#ifndef MAUL_UI_GLYPH_IMAGE_H
#define MAUL_UI_GLYPH_IMAGE_H

#include "maul-ui/base.h"
#include "maul-ui/text.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // The largest em a glyph image is rendered at, in device pixels.
#define MUI_MAX_GLYPH_PIXEL_SIZE 4096.0f

    // Where a glyph image goes and how large it is, in device pixels: its
    // left edge right of the pen and its top edge above the baseline.
    typedef struct muiGlyphImage
    {
        int32_t left;
        int32_t top;
        uint32_t width;
        uint32_t height;
    } muiGlyphImage;

    /// Renders a glyph as coverage: a byte per pixel, rows from the top,
    /// 0 outside the outline to 255 inside, linear in the area covered
    /// (a renderer applies any gamma). A glyph with no outline, such as
    /// a space, has an empty image. A glyph run's glyph at (x, y) from
    /// its origin, drawn at a scale, has its pen at (originX + x) * scale
    /// and its baseline at (originY + y) * scale, y rounded to a pixel;
    /// its em is the run's size times the scale.
    ///
    /// @param service    The service.
    /// @param font       A font key, as a glyph run carries; 0 for the
    ///                   default font.
    /// @param glyph      A glyph id of the font.
    /// @param pixelSize  The em in device pixels, from 1/64 to
    ///                   MUI_MAX_GLYPH_PIXEL_SIZE.
    /// @param offsetX    How far the pen is right of a pixel boundary,
    ///                   from 0 up to 1; the image's left is counted
    ///                   from that boundary.
    /// @param imageOut   Receives the image's place and size, also when
    ///                   pixels hold too few bytes.
    /// @param pixels     Receives width * height bytes; may be NULL when
    ///                   capacity is 0.
    /// @param capacity   How many bytes pixels holds.
    /// @return `mui_success`; `mui_errorCapacity` when pixels hold fewer
    ///         bytes than imageOut asks for, or memory runs out;
    ///         `mui_errorInvalid` for a NULL service or imageOut, NULL
    ///         pixels with a capacity, a size or offset outside the above,
    ///         or a glyph id the font does not have; `mui_errorStale` for
    ///         a key that names no font; `mui_errorFormat` for a glyph
    ///         whose outline cannot be read or is too large to render.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiRenderGlyph(muiTextService* service, uint64_t font,
                                                   uint32_t glyph, float pixelSize, float offsetX,
                                                   muiGlyphImage* imageOut, unsigned char* pixels,
                                                   size_t capacity);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_GLYPH_IMAGE_H
