// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Range values (record mui-0007): a value between a minimum and a
// maximum on a node, as a slider, a scrollbar or a volume control holds
// one. Unhandled input changes it by default: keys by ARIA's slider
// pattern along the range's axis (muiKeyInput, muiNavigationInput),
// and pointer records a host dispatches (muiDispatchPointerRecord): a
// press on the track pages toward the point, and a drag of the range
// (its node takes drags, maul-ui/interaction.h) moves the value with
// the pointer, keeping where the thumb was grabbed. A change made so is
// reported by mui_notificationRangeChanged; a change by code is not.

#ifndef MAUL_UI_RANGE_H
#define MAUL_UI_RANGE_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Which way a range runs.
    typedef uint8_t muiRangeAxis;

    enum
    {
        // The minimum at the inline start: on the left, or on the right
        // under right to left.
        mui_rangeHorizontal = 0,
        // The minimum at the bottom.
        mui_rangeVertical = 1,
    };

    typedef struct muiValueRange
    {
        float minimum;
        // At least the minimum.
        float maximum;
        // Kept within the two, on a step.
        float value;
        // Values lie on the minimum plus whole steps, as HTML's range
        // input's do, and an arrow moves one; 0 allows any value, an
        // arrow then moving a hundredth of the span. Finite and at least
        // 0.
        float step;
        // What page keys and a press on the track move. Finite and at
        // least 0.
        float page;
        muiRangeAxis axis;
        // The node inside the range the pointer grabs, whose length along
        // the axis the value's travel leaves room for; the null id for
        // none, the value then following the pointer.
        muiNodeId thumb;
    } muiValueRange;

    /// The default range: 0 to 100 by steps of 1, a page of 10,
    /// horizontal, at 0, without a thumb.
    ///
    /// @return The range.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiValueRange muiDefaultValueRange(void);

    /// Makes a node a range, or sets its range anew; the value is kept
    /// within the minimum and maximum and on a step.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param range    The range: finite numbers as described above.
    /// @return `mui_success`; `mui_errorCapacity` when the context holds
    ///         its limit of ranges; `mui_errorInvalid` for a NULL
    ///         argument, the null id, a range outside the above, or a call
    ///         from a measure or paint function; `mui_errorStale` for a
    ///         node or thumb that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetValueRange(muiContext* context, muiNodeId nodeId,
                                                          const muiValueRange* range);

    /// Reads a node's range.
    ///
    /// @param context   The context.
    /// @param nodeId    The node.
    /// @param rangeOut  Receives the range.
    /// @return `mui_success`; `mui_empty` for a node that is not a range;
    ///         `mui_errorInvalid` for a NULL argument or the null id;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetValueRange(const muiContext* context,
                                                          muiNodeId nodeId,
                                                          muiValueRange* rangeOut);

    /// Sets a range's value, kept within its minimum and maximum and on a
    /// step.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param value    The value, finite.
    /// @return `mui_success`; `mui_empty` for a node that is not a range;
    ///         `mui_errorInvalid` for a NULL context, the null id, a value
    ///         not finite or a call from a measure or paint function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetRangeValue(muiContext* context, muiNodeId nodeId,
                                                          float value);

    /// Makes a node no longer a range.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`, whether or not it was one;
    ///         `mui_errorInvalid` for a NULL context, the null id or a call
    ///         from a measure or paint function; `mui_errorStale` for a
    ///         node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_ClearValueRange(muiContext* context, muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_RANGE_H
