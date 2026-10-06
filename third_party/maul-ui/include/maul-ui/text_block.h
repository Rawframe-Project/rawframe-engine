// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text blocks (record mui-0006): UTF-8 text the text service lays out as
// a node's host content. A node whose host key is a block's key, and
// whose content is the host's, is measured by muiMeasureText and painted
// by muiPaintText in its computed text style: lines broken where Unicode
// allows (UAX #14), runs ordered by the Unicode bidirectional algorithm
// (UAX #9), glyphs shaped by HarfBuzz. White space is kept as written,
// line breaks in the text end lines, and spaces ending a wrapped line
// hang past it.

#ifndef MAUL_UI_TEXT_BLOCK_H
#define MAUL_UI_TEXT_BLOCK_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/text.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A text block, in the shape of every id (family record 0016).
    typedef struct muiTextBlockId
    {
        uint32_t index1;
        uint32_t generation;
    } muiTextBlockId;

    // What muiMeasureText and muiPaintText take as their user pointer:
    // the service whose blocks the host keys name, and the context whose
    // nodes' text style they read.
    typedef struct muiTextHost
    {
        muiTextService* service;
        const muiContext* context;
    } muiTextHost;

    /// Creates a text block holding a copy of UTF-8 text. Ill-formed
    /// sequences are laid out as U+FFFD.
    ///
    /// @param service  The service.
    /// @param text     The text; may be NULL when length is 0.
    /// @param length   Its length in bytes, below 2^31.
    /// @param blockOut Receives the block's id; the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         length of 2^31 or more; `mui_errorCapacity` when the
    ///         service's limit of blocks is reached or memory runs out.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateTextBlock(muiTextService* service, const char* text,
                                                       size_t length, muiTextBlockId* blockOut);

    /// Destroys a text block.
    ///
    /// @param service  The service.
    /// @param blockId  The block.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service or the
    ///         null id; `mui_errorStale` for a block that is gone.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyTextBlock(muiTextService* service,
                                                        muiTextBlockId blockId);

    /// Replaces a block's text with a copy of other UTF-8 text. Nodes that
    /// show it are measured again once the host calls
    /// muiNode_MarkContentChanged for them.
    ///
    /// @param service  The service.
    /// @param blockId  The block.
    /// @param text     The text; may be NULL when length is 0.
    /// @param length   Its length in bytes, below 2^31.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service, the
    ///         null id, a NULL text with a length or a length of 2^31 or
    ///         more; `mui_errorStale` for a block that is gone;
    ///         `mui_errorCapacity` when memory runs out, which keeps the
    ///         old text.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_SetText(muiTextService* service,
                                                         muiTextBlockId blockId, const char* text,
                                                         size_t length);

    /// Replaces the bytes of a block's text from start up to end with a
    /// text, as editing does; its nodes are measured and painted anew
    /// once marked changed. Offsets inside a UTF-8 sequence leave bytes
    /// that read as U+FFFD; muiTextBlock_FindDeletion and muiTextMove give
    /// offsets on grapheme cluster boundaries.
    ///
    /// @param service  The service.
    /// @param blockId  The block.
    /// @param start    The first byte replaced.
    /// @param end      The byte after the last; start for an insertion.
    /// @param text     The text put in its place, UTF-8. May be NULL when
    ///                 length is 0, and may be part of the block's text.
    /// @param length   Its length in bytes.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service, the
    ///         null id, a NULL text with a length, start after end, end
    ///         past the text, or a result of 2^31 bytes or more;
    ///         `mui_errorStale` for a block that is gone;
    ///         `mui_errorCapacity` when memory runs out, which keeps the
    ///         old text.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_Replace(muiTextService* service,
                                                         muiTextBlockId blockId, uint32_t start,
                                                         uint32_t end, const char* text,
                                                         size_t length);

    /// Reads a block's text.
    ///
    /// @param service    The service.
    /// @param blockId    The block.
    /// @param textOut    Receives its bytes, valid until its text is set,
    ///                   replaced or the block destroyed; never NULL.
    /// @param lengthOut  Receives its length in bytes.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for a block that is gone. Nothing
    ///         is written on failure.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_GetText(const muiTextService* service,
                                                         muiTextBlockId blockId,
                                                         const char** textOut, size_t* lengthOut);

    /// Returns a block's key, for a node's host key: never 0.
    ///
    /// @param blockId  The block.
    /// @return The key.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API uint64_t muiTextBlock_GetKey(muiTextBlockId blockId);

    /// Returns a font's key, for a text style's font: never 0, which names
    /// the service's default font. Text laid out in it draws glyph runs
    /// whose keys name an instance of the font as well: the axes and the
    /// bold or oblique the style's weight, slant and size make of it,
    /// which glyph images and atlases rebuild from the key. A null id's
    /// key is 0.
    ///
    /// @param fontId  The font.
    /// @return The key.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API uint64_t muiFont_GetKey(muiFontId fontId);

    /// Returns a font family's key, for a text style's font: never 0, and
    /// apart from every font's key.
    ///
    /// @param familyId  The family.
    /// @return The key; 0 for the null id.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API uint64_t muiFontFamily_GetKey(muiFontFamilyId familyId);

    /// Sets the font a text style's font key 0 names; the null id sets
    /// none, and text in font 0 then draws nothing.
    ///
    /// @param service  The service.
    /// @param fontId   The font, or the null id.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service;
    ///         `mui_errorStale` for a font that is gone.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetDefaultFont(muiTextService* service, muiFontId fontId);

    /// Sets the fonts tried, in order, for characters a style's font or
    /// family, and its family's fallbacks, lack: each grapheme cluster is
    /// drawn in the first that has all its characters, characters of no
    /// one script staying in the font before them when it has them.
    /// Lines keep the metrics of the style's own font.
    ///
    /// @param service  The service.
    /// @param keys     Keys of fonts and families; may be NULL when count
    ///                 is 0.
    /// @param count    Up to 8; 0 for none.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service, NULL
    ///         keys with a count, a count past 8 or a key that is no
    ///         font's or family's; `mui_errorStale` for one that is gone.
    ///         On failure the fallbacks stay as they were.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetFallbackFonts(muiTextService* service,
                                                        const uint64_t* keys, uint32_t count);

    /// Counts the times a block could not be laid out for want of memory,
    /// and so measured as empty and painted nothing.
    ///
    /// @param service  The service; NULL gives 0.
    /// @return The count.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_API uint64_t muiGetTextServiceFailures(const muiTextService* service);

    /// Measures a text block's lines, as a muiMeasureFunction: user is a
    /// muiTextHost, and hostKey a block's key. The width breaks lines:
    /// exact and at-most sizes wrap them to it, max-content keeps only the
    /// text's own line breaks, min-content breaks at every opportunity. The
    /// height is the lines' heights. A key that names no block, a font
    /// key that names no font, or a node whose text does not wrap measure
    /// as their lines allow; none of them is an error.
    ///
    /// @param user     A muiTextHost.
    /// @param nodeId   The node, whose text style is read.
    /// @param hostKey  The block's key.
    /// @param width    The width request.
    /// @param height   The height request, which the text does not use.
    /// @return The content box size.
    /// @par Thread safety
    /// Safe from any thread; the service and context are used by one
    /// thread at a time.
    MUI_API muiSize muiMeasureText(void* user, muiNodeId nodeId, uint64_t hostKey,
                                   muiMeasureAxis width, muiMeasureAxis height);

    /// Paints a text block's lines into a draw list, as a
    /// muiPaintFunction: user is a muiTextHost, and hostKey a block's key.
    /// Lines break to the content box's width, are ordered for display and
    /// aligned by the text style, and are drawn as glyph runs.
    ///
    /// @param user     A muiTextHost.
    /// @param nodeId   The node, whose text style is read.
    /// @param hostKey  The block's key.
    /// @param width    The content box's width.
    /// @param height   The content box's height.
    /// @param sink     Where the runs go.
    /// @par Thread safety
    /// Safe from any thread; the service and context are used by one
    /// thread at a time.
    MUI_API void muiPaintText(void* user, muiNodeId nodeId, uint64_t hostKey, float width,
                              float height, muiDrawSink* sink);

    /// Returns the baseline of a text block's first line, as a
    /// muiBaselineFunction: user is a muiTextHost, and hostKey a block's
    /// key. Lines are a line height apart from the content box's top, so
    /// the first baseline does not depend on the width.
    ///
    /// @param user     A muiTextHost.
    /// @param nodeId   The node, whose text style is read.
    /// @param hostKey  The block's key.
    /// @param width    The content box's width, which the baseline does not
    ///                 use.
    /// @param height   The content box's height, which it does not use.
    /// @return The baseline's distance down from the content box's top;
    ///         NaN for empty text, which has no lines, and where
    ///         muiMeasureText measures nothing.
    /// @par Thread safety
    /// Safe from any thread; the service and context are used by one
    /// thread at a time.
    MUI_API float muiTextBaseline(void* user, muiNodeId nodeId, uint64_t hostKey, float width,
                                  float height);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TEXT_BLOCK_H
