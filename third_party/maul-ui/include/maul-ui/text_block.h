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

    /// Returns a block's key, for a node's host key: never 0.
    ///
    /// @param blockId  The block.
    /// @return The key.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API uint64_t muiTextBlock_GetKey(muiTextBlockId blockId);

    /// Returns a font's key, for a text style's font: never 0, which names
    /// the service's default font.
    ///
    /// @param fontId  The font.
    /// @return The key.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API uint64_t muiFont_GetKey(muiFontId fontId);

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

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TEXT_BLOCK_H
