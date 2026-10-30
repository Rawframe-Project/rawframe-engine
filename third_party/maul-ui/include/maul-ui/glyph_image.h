// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph images: a glyph of a text service's font rendered for a renderer
// to draw, as 8-bit coverage or as a signed distance field that scales.
// Outlines are rendered unhinted, as text is laid out, so an image sits
// where its glyph run places it at every size; images are the same on
// every platform.

#ifndef MAUL_UI_GLYPH_IMAGE_H
#define MAUL_UI_GLYPH_IMAGE_H

#include "maul-ui/base.h"
#include "maul-ui/draw.h"
#include "maul-ui/text.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // The smallest and largest em a glyph image is rendered at, in device
    // pixels.
#define MUI_MIN_GLYPH_PIXEL_SIZE 0.015625f
#define MUI_MAX_GLYPH_PIXEL_SIZE 4096.0f

    // The range of a distance field's spread, in pixels of its image.
#define MUI_MIN_FIELD_SPREAD 2u
#define MUI_MAX_FIELD_SPREAD 32u

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
    /// a space or any glyph of a font of bitmaps alone, has an empty
    /// image. A glyph run's glyph at (x, y) from
    /// its origin, drawn at a scale, has its pen at (originX + x) * scale
    /// and its baseline at (originY + y) * scale, y rounded to a pixel;
    /// its em is the run's size times the scale.
    ///
    /// @param service    The service.
    /// @param font       A font key, as a glyph run carries; 0 for the
    ///                   default font.
    /// @param glyph      A glyph id of the font.
    /// @param pixelSize  The em in device pixels, from
    ///                   MUI_MIN_GLYPH_PIXEL_SIZE to MUI_MAX_GLYPH_PIXEL_SIZE.
    /// @param offsetX    How far the pen is right of a pixel boundary,
    ///                   from 0 up to 1; the image's left is counted
    ///                   from that boundary.
    /// @param imageOut   Receives the image's place and size, also when pixelsOut
    ///                   holds too few bytes. Zeroed on any other failure.
    /// @param pixelsOut  Receives width * height bytes; may be NULL when
    ///                   capacity is 0.
    /// @param capacity   How many bytes pixelsOut holds.
    /// @return `mui_success`; `mui_errorCapacity` when pixelsOut holds fewer bytes
    ///         than imageOut asks for, or memory runs out; `mui_errorInvalid`
    ///         for a NULL service or imageOut, NULL pixelsOut with a capacity, a
    ///         size that is not a finite number above 0, an offset outside the
    ///         above, or a glyph id the font does not have; `mui_errorLimit`
    ///         for a size outside the above; `mui_errorStale` for a key that
    ///         names no font; `mui_errorFormat` for a glyph whose outline
    ///         cannot be read or is too large to render; `mui_errorBusy`
    ///         while the service is busy.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextService_RenderGlyph(
        muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize, float offsetX,
        muiGlyphImage* imageOut, unsigned char* pixelsOut, size_t capacity);

    /// Renders a colour glyph (COLR, with its CPAL palettes) at a size,
    /// placed as muiTextService_RenderGlyph places coverage: four bytes a pixel, rows
    /// from the top, red, green, blue and alpha. A version 1 glyph's paint
    /// graph is drawn where the glyph has one: its layers, solid fills,
    /// gradients, glyph outlines, other colour glyphs, transforms,
    /// composites and clip box. A version 0 glyph's layers are each another
    /// glyph's outline filled with its palette entry. Entry 0xFFFF is the
    /// text's colour, and everything is composited in premultiplied linear
    /// light; the pixels are stored as an sRGB texture holds premultiplied
    /// colour, red, green and blue encoded with sRGB's transfer function
    /// and alpha linear. A glyph without COLR colour is drawn from its
    /// colour bitmap where the font has one (CBLC and CBDT, or sbix), from
    /// the strike that suits the size, scaled to it. A glyph without colour
    /// gives `mui_empty` and an empty image, to be drawn as coverage.
    ///
    /// @param service     The service.
    /// @param font        A font key, as a glyph run carries; 0 for the
    ///                    default font.
    /// @param glyph       A glyph id of the font.
    /// @param pixelSize   The em in device pixels, from
    ///                    MUI_MIN_GLYPH_PIXEL_SIZE to MUI_MAX_GLYPH_PIXEL_SIZE.
    /// @param offsetX     How far the pen is right of a pixel boundary,
    ///                    from 0 up to 1, as muiTextService_RenderGlyph takes it.
    /// @param palette     The font's palette to fill from; the first for one
    ///                    the font does not have.
    /// @param foreground  The text's colour, linear and premultiplied.
    /// @param imageOut    Receives the image's place and size, also when pixelsOut
    ///                    holds too few bytes. Zeroed on any other failure.
    /// @param pixelsOut   Receives width * height * 4 bytes; may be NULL
    ///                    when capacity is 0.
    /// @param capacity    How many bytes pixelsOut holds.
    /// @return `mui_success`; `mui_empty` for a glyph without colour;
    ///         `mui_errorFormat` for a paint graph more than 64 paints deep, as
    ///         one that paints itself is, a clip box whose maximum is below
    ///         its minimum, or a bitmap of 2^28 pixels or more, as 16384 by
    ///         16384 is; otherwise as muiTextService_RenderGlyph;
    ///         `mui_errorBusy` while the service is busy.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextService_RenderColorGlyph(
        muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize, float offsetX,
        uint32_t palette, muiLinearColor foreground, muiGlyphImage* imageOut,
        unsigned char* pixelsOut, size_t capacity);

    /// Renders a glyph as a signed distance field, which a renderer scales
    /// to any size: a byte per pixel, rows from the top, 128 at the
    /// outline, and 128 / spread more for each pixel inside and less for
    /// each pixel outside, up to 255 and down to 0. The image reaches
    /// spread pixels past the outline on every side; drawn at a scale, it
    /// covers where a sample is 128 or more. Glyphs whose contours overlap,
    /// as variable fonts' and composite glyphs' may, are rendered as their
    /// union.
    ///
    /// @param service    The service.
    /// @param font       A font key, as a glyph run carries; 0 for the
    ///                   default font.
    /// @param glyph      A glyph id of the font.
    /// @param pixelSize  The em in pixels of the image, from
    ///                   MUI_MIN_GLYPH_PIXEL_SIZE to MUI_MAX_GLYPH_PIXEL_SIZE.
    /// @param spread     How far the field reaches past the outline, from
    ///                   MUI_MIN_FIELD_SPREAD to MUI_MAX_FIELD_SPREAD
    ///                   pixels.
    /// @param imageOut   Receives the image's place and size, from the pen and
    ///                   baseline at pixelSize, also when pixelsOut holds too few
    ///                   bytes. Zeroed on any other failure.
    /// @param pixelsOut  Receives width * height bytes; may be NULL when
    ///                   capacity is 0.
    /// @param capacity   How many bytes pixelsOut holds.
    /// @return `mui_success`; `mui_errorCapacity` when pixelsOut holds fewer bytes
    ///         than imageOut asks for, or memory runs out; `mui_errorInvalid`
    ///         for a NULL service or imageOut, NULL pixelsOut with a capacity, a
    ///         size that is not a finite number above 0, or a glyph id the
    ///         font does not have; `mui_errorLimit` for a size or spread
    ///         outside the above; `mui_errorStale` for a key that names no
    ///         font; `mui_errorFormat` for a glyph whose outline cannot be read
    ///         or is too large to render; `mui_errorBusy` while the service is
    ///         busy.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextService_RenderGlyphField(
        muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize, uint32_t spread,
        muiGlyphImage* imageOut, unsigned char* pixelsOut, size_t capacity);

    /// Renders a glyph's multi-channel signed distance field (MTSDF) for a
    /// renderer that scales it: four bytes a pixel. Red, green and blue
    /// are three distances, each to the outline's edges of one colour, and
    /// their median is the distance with the outline's corners kept sharp
    /// at any scale (Chlumský's method, as msdfgen's); alpha is the field
    /// muiTextService_RenderGlyphField renders, byte for byte, for outlines, glows and
    /// shadows. Each channel is 128 at its distance's 0 and 128 / spread
    /// more for each pixel inside, less outside, held within 0 and 255.
    /// The image is placed and sized as muiTextService_RenderGlyphField places it, and
    /// overlapping contours are their union.
    ///
    /// @param service    The service.
    /// @param font       A font key, as a glyph run carries; 0 for the
    ///                   default font.
    /// @param glyph      A glyph id of the font.
    /// @param pixelSize  The em in pixels of the image, from
    ///                   MUI_MIN_GLYPH_PIXEL_SIZE to MUI_MAX_GLYPH_PIXEL_SIZE.
    /// @param spread     How far the field reaches past the outline, from
    ///                   MUI_MIN_FIELD_SPREAD to MUI_MAX_FIELD_SPREAD
    ///                   pixels.
    /// @param imageOut   Receives the image's place and size, also when pixelsOut
    ///                   holds too few bytes. Zeroed on any other failure.
    /// @param pixelsOut  Receives width * height * 4 bytes, red, green, blue
    ///                   and alpha a pixel; may be NULL when capacity is 0.
    /// @param capacity   How many bytes pixelsOut holds.
    /// @return As muiTextService_RenderGlyphField; `mui_errorBusy` while the service is
    ///         busy.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextService_RenderGlyphMultiField(
        muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize, uint32_t spread,
        muiGlyphImage* imageOut, unsigned char* pixelsOut, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_GLYPH_IMAGE_H
