// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Targets: one animated value of a fixed
// kind, holding the host's base value, the current value animations
// compose over it and its velocity. Animations write targets; the host
// reads them by id.

#ifndef MAUL_TWEEN_TARGET_H
#define MAUL_TWEEN_TARGET_H

#include "maul-tween/base.h"
#include "maul-tween/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A target as it is: its kind, base value, current value and velocity
    // (channels per second). The value and velocity are always finite: one
    // driven past the floats' range is held at the nearest finite float,
    // and still while held.
    typedef struct mtwnTargetState
    {
        mtwnVec4 base;
        mtwnVec4 value;
        mtwnVec4 velocity;
        mtwnValueKind kind;
    } mtwnTargetState;

    /// Makes a target holding a value as its base and current value, at
    /// rest. Channels past the kind's are stored as 0.
    ///
    /// @param context   The context.
    /// @param kind      The kind.
    /// @param value     The value: finite in the kind's channels, for a
    ///                  rotation not all zero, for a scale above 0. Stored
    ///                  as given.
    /// @param targetOut Receives the target's id, or the null id on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null argument, an
    ///         unknown kind or a value outside the above;
    ///         mtwn_errorCapacity when the context holds its capacity of
    ///         targets.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnCreateTarget(mtwnContext* context, mtwnValueKind kind,
                                                        mtwnVec4 value, mtwnTargetId* targetOut);

    /// Destroys a target; its id and every copy of it become stale.
    ///
    /// @param context  The context.
    /// @param target   The target.
    /// @return mtwn_success; mtwn_errorInvalid for a null context;
    ///         mtwn_errorStale for an id that names no live target.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnDestroyTarget(mtwnContext* context, mtwnTargetId target);

    /// Tells whether an id names a live target. Not counted as misuse.
    ///
    /// @param context  The context, or NULL.
    /// @param target   The id.
    /// @return Whether the target is alive.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API bool mtwnTarget_IsValid(const mtwnContext* context, mtwnTargetId target);

    /// Sets a target's base value, the value animations compose over. A
    /// target no animation writes shows it at once; the next update
    /// reports the target as changed.
    ///
    /// @param context  The context.
    /// @param target   The target.
    /// @param value    The value, as for mtwnCreateTarget.
    /// @return mtwn_success; mtwn_errorInvalid for a null context or a value
    ///         its kind refuses; mtwn_errorStale for an id that names no
    ///         live target.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnTarget_SetBase(mtwnContext* context, mtwnTargetId target,
                                                          mtwnVec4 value);

    /// Reads a target's current value.
    ///
    /// @param context   The context.
    /// @param target    The target.
    /// @param valueOut  Receives the value, zero on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null argument;
    ///         mtwn_errorStale for an id that names no live target.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnTarget_GetValue(mtwnContext* context,
                                                           mtwnTargetId target, mtwnVec4* valueOut);

    /// Reads all of a target: kind, base value, current value and velocity.
    ///
    /// @param context   The context.
    /// @param target    The target.
    /// @param stateOut  Receives the state, zero on failure.
    /// @return As mtwnTarget_GetValue.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnTarget_GetState(mtwnContext* context,
                                                           mtwnTargetId target,
                                                           mtwnTargetState* stateOut);

    /// Reads many targets' current values, in the order of their ids.
    ///
    /// @param context    The context.
    /// @param targets    The ids; NULL only with a count of 0.
    /// @param count      How many.
    /// @param valuesOut  Receives count values, zero for an id that names
    ///                   no live target; nothing written on the other
    ///                   failures.
    /// @return mtwn_success; mtwn_errorInvalid for a null argument;
    ///         mtwn_errorStale when any id names no live target, each such id
    ///         counted as misuse, the others read.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnContext_ReadTargets(mtwnContext* context,
                                                               const mtwnTargetId* targets,
                                                               uint32_t count, mtwnVec4* valuesOut);

#ifdef __cplusplus
}
#endif

#endif
