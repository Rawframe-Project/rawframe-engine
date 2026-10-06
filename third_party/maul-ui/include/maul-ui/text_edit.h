// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Editing primitives over laid-out text (record mui-0006): positions in a
// node's text, from points, to carets and moved by cluster, word or line,
// the rectangles a range of it covers, the text laid out as muiPaintText
// paints it, what a deletion removes, and an input method's composition
// held in a block. Selection, input and undo are the caller's.

#ifndef MAUL_UI_TEXT_EDIT_H
#define MAUL_UI_TEXT_EDIT_H

#include "maul-ui/base.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text_block.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Which side of an offset a position keeps to, where the offset has
    // two places: the end of a wrapped line or the start of the next, and
    // either side of a change of direction.
    typedef uint8_t muiTextAffinity;

    enum
    {
        // With the text after the offset.
        mui_affinityDownstream = 0,
        // With the text before it.
        mui_affinityUpstream = 1,
    };

    // A place between grapheme clusters of a node's text: a byte offset
    // into its block's UTF-8 text, and its affinity.
    typedef struct muiTextPosition
    {
        uint32_t offset;
        muiTextAffinity affinity;
    } muiTextPosition;

    // Where a position moves to. Words are UAX #29 word segments with a
    // letter or a number in them.
    typedef uint8_t muiTextMovement;

    enum
    {
        // The next grapheme cluster boundary in the text.
        mui_moveNextCluster = 0,
        // The one before.
        mui_movePreviousCluster = 1,
        // One cluster left on screen, as the text is drawn; from a line's
        // left end, the line before's end (left to right paragraphs) or
        // the next line's start (right to left ones).
        mui_moveLeft = 2,
        // One cluster right on screen; from a line's right end, the next
        // line's start or the line before's end.
        mui_moveRight = 3,
        // The start of the next word, after the position (Windows'
        // Ctrl+Right).
        mui_moveNextWordStart = 4,
        // The end of the word the position is in, or of the next (macOS's
        // Option+Right).
        mui_moveNextWordEnd = 5,
        // The start of the word the position is in, or of the one before.
        mui_movePreviousWordStart = 6,
        // The start of the position's line in the text.
        mui_moveLineStart = 7,
        // Its end, before any white space hanging past it, keeping to the
        // line.
        mui_moveLineEnd = 8,
        // The line above, at the preferred x; from the first line, the
        // text's start.
        mui_moveLineUp = 9,
        // The line below; from the last line, the text's end.
        mui_moveLineDown = 10,
        mui_moveTextStart = 11,
        mui_moveTextEnd = 12,
    };

    // Which way a deletion from a position goes.
    typedef uint8_t muiTextDeletion;

    enum
    {
        // Back, as Backspace deletes: one code point, so a mistyped mark
        // or jamo goes alone; but a cluster with an emoji, a regional
        // indicator or a keycap goes whole, a variation selector with the
        // code point before it, and a CR with its LF.
        mui_deleteBackward = 0,
        // Forward, as Delete does: the next grapheme cluster.
        mui_deleteForward = 1,
    };

    // How a part of an input method's composition is drawn.
    typedef uint8_t muiCompositionStyle;

    enum
    {
        // Not underlined.
        mui_compositionPlain = 0,
        // A thin underline: text still to convert.
        mui_compositionUnderline = 1,
        // A thick underline: the part a conversion works on now.
        mui_compositionTarget = 2,
        // A thin underline: converted, not yet committed.
        mui_compositionConverted = 3,
    };

    enum
    {
        // The most segments a composition may have.
        MUI_MAX_COMPOSITION_SEGMENTS = 32
    };

    // A styled part of a composition, in bytes of its text.
    typedef struct muiCompositionSegment
    {
        uint32_t start;
        uint32_t length;
        muiCompositionStyle style;
    } muiCompositionSegment;

    // Where a caret is drawn in the node's content box: its x, the top
    // and height of its line, and whether the text it sits on runs right
    // to left.
    typedef struct muiTextCaret
    {
        float x;
        float y;
        float height;
        bool rightToLeft;
    } muiTextCaret;

    /// Finds the position nearest a point of a node's text: the line at
    /// the point's y (the first above the text, the last below it), then
    /// the edge of the grapheme cluster nearer the point's x (the right
    /// one at the middle), a cluster several clusters share a glyph with
    /// taking an equal share of it; past a line's ends, that end, before
    /// any white space hanging past it. The position keeps to the cluster
    /// the point is on. Negative letter spacing can draw a cluster over
    /// the one before it; the point then finds the first, left to right.
    ///
    /// @param host         The text host the node's text is laid out with.
    /// @param nodeId       A node whose host key is a block's.
    /// @param width        The node's content box width, as painting is
    ///                     given.
    /// @param x            The point, from the content box's top left.
    /// @param y            Likewise.
    /// @param positionOut  Receives the position; unchanged on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         point not finite; `mui_errorStale` for a node, block or font
    ///         that is gone; `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextHitTest(const muiTextHost* host, muiNodeId nodeId,
                                                   float width, float x, float y,
                                                   muiTextPosition* positionOut);

    /// Finds where the caret of a position is drawn: at the leading edge
    /// of the cluster after it for downstream, the trailing edge of the
    /// cluster before it for upstream, on the line the affinity picks
    /// where a line wraps; an offset in white space hanging past a line's
    /// end sits at that end, and an offset inside a cluster at its start.
    ///
    /// @param host      The text host.
    /// @param nodeId    A node whose host key is a block's.
    /// @param width     The node's content box width.
    /// @param position  The position; an offset past the text is its end.
    /// @param caretOut  Receives the caret; unchanged on failure.
    /// @return As muiTextHitTest.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextGetCaret(const muiTextHost* host, muiNodeId nodeId,
                                                    float width, muiTextPosition position,
                                                    muiTextCaret* caretOut);

    /// Finds the rectangles a range of a node's text covers: on each line,
    /// one for each stretch of side by side clusters in the range, left to
    /// right, lines from the top; clusters on both sides of the range's
    /// ends are not covered.
    ///
    /// @param host      The text host.
    /// @param nodeId    A node whose host key is a block's.
    /// @param width     The node's content box width.
    /// @param start     The range's first byte.
    /// @param end       The byte after it; no rectangles when not after
    ///                  start.
    /// @param rects     Receives the rectangles, from the content box's top
    ///                  left; may be NULL when capacity is 0.
    /// @param capacity  How many rects holds.
    /// @param countOut  Receives how many rectangles there are, also when
    ///                  rects holds fewer.
    /// @return `mui_success`; `mui_errorCapacity` when rects holds fewer,
    ///         writing those that fit, or memory runs out; otherwise as
    ///         muiTextHitTest.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextGetRangeRects(const muiTextHost* host, muiNodeId nodeId,
                                                         float width, uint32_t start, uint32_t end,
                                                         muiRect* rects, uint32_t capacity,
                                                         uint32_t* countOut);

    /// Moves a position through a node's text, laid out as muiPaintText
    /// paints it. Positions moved to are on grapheme cluster boundaries;
    /// one that cannot move (the text's start moving back) stays.
    ///
    /// @param host         The text host.
    /// @param nodeId       A node whose host key is a block's.
    /// @param width        The node's content box width.
    /// @param from         The position; an offset past the text is its end.
    /// @param movement     Where to.
    /// @param preferredX   For moving up and down a line, the x to keep, as
    ///                     the caret had before the first vertical move;
    ///                     NaN for from's own caret x. Not read otherwise.
    /// @param positionOut  Receives the position; unchanged on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         movement out of range; `mui_errorStale` for a node, block or
    ///         font that is gone; `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Safe from any thread; the host's context and service are used by
    /// one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextMove(const muiTextHost* host, muiNodeId nodeId,
                                                float width, muiTextPosition from,
                                                muiTextMovement movement, float preferredX,
                                                muiTextPosition* positionOut);

    /// Finds the bytes a deletion from an offset of a block's text
    /// removes, for muiTextBlock_Replace; words are deleted by moving
    /// with muiTextMove and replacing what lies between. Nothing at the
    /// text's start going back or its end going forward.
    ///
    /// @param service   The service.
    /// @param blockId   The block.
    /// @param offset    The offset; past the text is its end.
    /// @param deletion  Which way.
    /// @param startOut  Receives the first byte to remove.
    /// @param endOut    Receives the byte after the last; startOut's value
    ///                  when there is nothing.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or a deletion out of range; `mui_errorStale` for a
    ///         block that is gone. Nothing is written on failure.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_FindDeletion(const muiTextService* service,
                                                              muiTextBlockId blockId,
                                                              uint32_t offset,
                                                              muiTextDeletion deletion,
                                                              uint32_t* startOut, uint32_t* endOut);

    /// Sets a block's input method composition, the text being composed
    /// before it is committed: the text replaces the composition there is,
    /// or goes in at offset when there is none, and painting underlines
    /// it by its segments (all of it thin when there are none). An empty
    /// text removes the composition and ends it. While it lasts,
    /// muiTextBlock_Replace before or after it moves it, and one that
    /// overlaps it, or muiTextBlock_SetText, ends it.
    ///
    /// @param service       The service.
    /// @param blockId       The block.
    /// @param offset        Where a new composition goes; past the text is
    ///                      its end. Not read while one lasts.
    /// @param text          The composition, UTF-8. May be NULL when
    ///                      length is 0.
    /// @param length        Its length in bytes.
    /// @param segments      Its styled parts, within it. May be NULL when
    ///                      segmentCount is 0.
    /// @param segmentCount  How many, at most MUI_MAX_COMPOSITION_SEGMENTS.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service, the
    ///         null id, a NULL text or segments with a count, too many
    ///         segments, one past the text or of an unknown style, or a
    ///         text of 2^31 bytes or more; `mui_errorStale` for a block
    ///         that is gone; `mui_errorCapacity` when memory runs out,
    ///         which keeps the old text and composition.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_SetComposition(
        muiTextService* service, muiTextBlockId blockId, uint32_t offset, const char* text,
        size_t length, const muiCompositionSegment* segments, uint32_t segmentCount);

    /// Ends a block's composition, keeping its text as typed text: what an
    /// input method that commits the composition as it is asks. Nothing
    /// without one.
    ///
    /// @param service  The service.
    /// @param blockId  The block.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL service or the
    ///         null id; `mui_errorStale` for a block that is gone.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_EndComposition(muiTextService* service,
                                                                muiTextBlockId blockId);

    /// Reads where a block's composition is.
    ///
    /// @param service    The service.
    /// @param blockId    The block.
    /// @param startOut   Receives its first byte in the text.
    /// @param lengthOut  Receives its length; 0 when there is none.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for a block that is gone. Nothing
    ///         is written on failure.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTextBlock_GetComposition(const muiTextService* service,
                                                                muiTextBlockId blockId,
                                                                uint32_t* startOut,
                                                                uint32_t* lengthOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TEXT_EDIT_H
