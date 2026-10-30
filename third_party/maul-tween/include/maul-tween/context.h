// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context: the owner of
// targets, clips and players, made from a def that names every capacity
// and allocated in one block through the host's allocator. Nothing
// allocates after creation.
//
// The host's code a context's functions run (a host easing, the
// allocator, the task system's functions and tasks) calls no function of
// that context: while one runs, those taking the context non-const refuse
// with mtwn_errorBusy and count one misuse, and the const ones read it in
// the middle of a call.

#ifndef MAUL_TWEEN_CONTEXT_H
#define MAUL_TWEEN_CONTEXT_H

#include "maul-tween/base.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A context. Made by mtwnCreateContext.
    typedef struct mtwnContext mtwnContext;

    // A target the context holds, by a generation-checked id; zero is null.
    typedef struct mtwnTargetId
    {
        uint32_t index1;
        uint32_t generation;
    } mtwnTargetId;

    // The most of each count a def may name, and the most clip storage.
#define MTWN_MAX_COUNT        16777216u
#define MTWN_MAX_CLIP_STORAGE 2147483648u

    // A range of work items, [startIndex, endIndex), run by the host on any
    // thread. The library passes taskContext through unchanged.
    typedef void mtwnTaskFunction(int32_t startIndex, int32_t endIndex, void* taskContext);

    // Hands the host itemCount items of work: the host splits [0, itemCount)
    // into ranges of at least minRange items (the last may be shorter),
    // runs task once on each range, on any thread and in any order, and
    // returns a handle that finishTask waits on. A host that ran every range
    // before returning may return NULL, and finishTask is then not called.
    // Results never depend on the split or the order: each item writes only
    // its own memory. Neither it nor the task calls a function of the
    // context.
    typedef void* mtwnEnqueueTaskFunction(mtwnTaskFunction* task, int32_t itemCount,
                                          int32_t minRange, void* taskContext, void* userContext);

    // Waits until every range of the work behind userTask has run.
    typedef void mtwnFinishTaskFunction(void* userTask, void* userContext);

    // What a context holds: its allocator, the host's task system and every
    // capacity. Start from mtwnDefaultContextDef and set what differs.
    typedef struct mtwnContextDef
    {
        // Set by mtwnDefaultContextDef; checked by mtwnCreateContext.
        uint32_t cookie;
        // The headers' MTWN_ABI_VERSION, set by mtwnDefaultContextDef: the
        // other defs are used under a context and carry only their cookie.
        uint32_t version;
        // The allocator; zeroed means the C library's.
        mtwnAllocator allocator;
        // The host's task system, which mtwnContext_Update hands the clips' players
        // to in up to 16 groups; both NULL, the default,
        // runs every player on the calling thread. One without the other is
        // invalid. With them the context holds each group's scratch, on a
        // 64-bit target: about 16 KB, 72 bytes for each event record (an
        // mtwnEvent and a double) up to 1,024 and 60 for each
        // of a sixteenth of twice the bindings (about 900 KB more for the
        // default def), and 36 bytes a player.
        mtwnEnqueueTaskFunction* enqueueTask;
        mtwnFinishTaskFunction* finishTask;
        // Passed to enqueueTask and finishTask as their userContext.
        void* userTaskContext;
        // Targets alive at once, up to MTWN_MAX_COUNT.
        uint32_t targetCapacity;
        // Players alive at once, transitions included, up to
        // MTWN_MAX_COUNT.
        uint32_t playerCapacity;
        // Clips alive at once, up to MTWN_MAX_COUNT.
        uint32_t clipCapacity;
        // Event records the queue holds between reads, up to
        // MTWN_MAX_COUNT; 0 makes none, and clips' players skip the
        // sweeps that find their crossings.
        uint32_t eventCapacity;
        // Bindings of slots to targets the players hold at once, up to
        // MTWN_MAX_COUNT.
        uint32_t bindingCapacity;
        // The most nodes a clip may have for players in step to share its
        // sample, and the most records a sweep keeps to share,
        // each up to MTWN_MAX_COUNT; 0 shares none of that
        // kind. The context holds 80 and 72 bytes for each on a 64-bit
        // target (a record is an mtwnEvent and a double), in its own work
        // and in each group's with a task system.
        uint32_t sharedNodeCapacity;
        uint32_t sharedRecordCapacity;
        // The most points of CSS linear() a transition's easing may have,
        // up to MTWN_MAX_COUNT: a transition copies them into the context,
        // which holds 16 bytes for each, for every player. A transition
        // whose easing has more is refused with mtwn_errorCapacity; a longer
        // table belongs in a clip (mtwnClipDef's pointCapacity). 0 takes no
        // linear() easing.
        uint32_t pointsPerTransition;
        // Bytes for the clips' contents, up to MTWN_MAX_CLIP_STORAGE.
        uint64_t clipStorageBytes;
    } mtwnContextDef;

// The cookie mtwnDefaultContextDef sets and mtwnCreateContext checks.
#define MTWN_CONTEXT_DEF_COOKIE 0x4E575454u

    /// Makes a context def for a medium host: 1024 targets, 256 players
    /// with 64 points of linear() for each transition, 16 clips, 256
    /// events, 1024 bindings, sharing for clips of up to 128 nodes and 64
    /// records, and 224 KiB of clip storage, which holds 16
    /// clips of the default clip def, the C library's allocator and no task
    /// system.
    /// Built in the program, so it carries the version of the headers the
    /// program includes.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mtwnContextDef mtwnDefaultContextDef(void)
    {
#ifdef __cplusplus
        mtwnContextDef def = {};
#else
    mtwnContextDef def = {0};
#endif
        def.cookie = MTWN_CONTEXT_DEF_COOKIE;
        def.version = MTWN_ABI_VERSION;
        def.targetCapacity = 1024;
        def.playerCapacity = 256;
        def.clipCapacity = 16;
        def.eventCapacity = 256;
        def.bindingCapacity = 1024;
        def.sharedNodeCapacity = 128;
        def.sharedRecordCapacity = 64;
        def.pointsPerTransition = 64;
        def.clipStorageBytes = 16u * 14336u;
        return def;
    }

    /// Makes a context: checks the def and allocates everything the context
    /// holds in one block.
    ///
    /// @param def         The def.
    /// @param contextOut  Receives the context, or NULL on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null argument or a def
    ///         without its cookie; then mtwn_errorVersion for a def stamped
    ///         by headers of another ABI version than the library's, before
    ///         anything else in it is read; mtwn_errorInvalid for an
    ///         allocator or a task system with one function of the two;
    ///         mtwn_errorLimit for a capacity past MTWN_MAX_COUNT or clip
    ///         storage past MTWN_MAX_CLIP_STORAGE; mtwn_errorCapacity when
    ///         the allocator fails or the block's size does not fit.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnCreateContext(const mtwnContextDef* def,
                                                         mtwnContext** contextOut);

    /// Destroys a context and everything it holds.
    ///
    /// @param context  The context, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API void mtwnDestroyContext(mtwnContext* context);

    /// Grows a context to a def's capacities, each at least the context's
    /// own but the sharing's, which may be any, and takes the def's task
    /// system, with the allocator it was made
    /// with: the one time after its creation it allocates, and only when
    /// asked (it never grows by itself). Its objects move to one new block, their ids unchanged,
    /// and the old block is freed, so the context's address changes: the call writes the new one.
    /// On any failure nothing changes.
    ///
    /// @param contextInOut  The context; receives the grown one.
    /// @param def           The capacities: start from the context's own.
    /// @return mtwn_success; mtwn_errorInvalid for a null argument, a def
    ///         without its cookie, a capacity below the context's, another
    ///         allocator, or a task system with one function of the two;
    ///         mtwn_errorLimit for a capacity past its maximum, as
    ///         mtwnCreateContext; mtwn_errorVersion for a def of another
    ///         ABI version; mtwn_errorCapacity when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnContext_Grow(mtwnContext** contextInOut,
                                                        const mtwnContextDef* def);

    /// Returns how many calls against the context were refused for an
    /// invalid argument, a stale id or a call back in from the host's code
    ///  since it was made.
    ///
    /// @param context  The context.
    /// @return The count.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API uint64_t mtwnContext_GetMisuse(const mtwnContext* context);

    /// Returns how many clip players' advances a context with a task system
    /// ran on the calling thread since it was made: a
    /// player whose clip has more than 128 nodes and other players, one
    /// whose targets of its
    /// own might not fit its group's staging, one whose staging ran out,
    /// and those after it in its group. A count that grows asks for larger
    /// binding or event capacities, or smaller clips.
    ///
    /// @param context  The context.
    /// @return The count; 0 without a task system.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API uint64_t mtwnContext_GetSerialAdvances(const mtwnContext* context);

    /// Returns the size of the block the context holds everything in.
    ///
    /// @param context  The context.
    /// @return Bytes.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API uint64_t mtwnContext_GetMemoryBytes(const mtwnContext* context);

    /// Turns reduced motion on or off; it is off when a context is made.
    /// While it is on, every transition and every running clip's player
    /// with a finite end finishes at the next update, its delay skipped and
    /// its events at that update's time, unless its def marks it
    /// essential; a player with an infinite end plays on, the host's to
    /// keep, seek or stop. Turning it off reaches what has not finished.
    /// The host reads the platform's preference and turns it on or off.
    ///
    /// @param context  The context.
    /// @param enabled  Whether motion is reduced.
    /// @return mtwn_success; mtwn_errorInvalid for a null context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnContext_EnableReducedMotion(mtwnContext* context,
                                                                       bool enabled);

    /// Returns whether reduced motion is on.
    ///
    /// @param context  The context, or NULL.
    /// @return Whether it is; false for a null context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API bool mtwnContext_IsReducedMotionEnabled(const mtwnContext* context);

    /// Returns how many clips' players the last update left running under
    /// reduced motion though not essential: those that could not finish,
    /// their end infinite or their rate 0. When it is not 0, the host finds
    /// them by mtwnPlayer_GetState's playsOnReduced and keeps, seeks or
    /// stops each.
    ///
    /// @param context  The context, or NULL.
    /// @return The count; 0 for a null context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API uint32_t mtwnContext_GetPlayingOnCount(const mtwnContext* context);

    /// Advances every player to the host's time, in the order they were
    /// made, later ones writing over earlier ones: transitions sample their
    /// curves, write their targets and end; clips' players sample their
    /// clips into their bound targets, and those that finish, unless kept,
    /// commit what they fill forward as their targets' bases and are freed.
    /// A target bound to a clip's player that no player writes shows its
    /// base. Reports the targets whose value
    /// or base changed since the last report, in the order they changed;
    /// a target appears once.
    ///
    /// @param context          The context.
    /// @param timeNs           The host's time in nanoseconds, never less
    ///                         than the last update's.
    /// @param changedOut       Receives the first changedCapacity changed
    ///                         targets; NULL only with a capacity of 0; nothing
    ///                         written on failure.
    /// @param changedCapacity  Its length.
    /// @param changedTotalOut  Receives how many targets changed, which may
    ///                         exceed the capacity; those past it are left out
    ///                         of the report only. May be NULL; 0 on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null context, a null
    ///         array with a capacity, or a time before the last update's,
    ///         nothing advanced.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnContext_Update(mtwnContext* context, uint64_t timeNs,
                                                          mtwnTargetId* changedOut,
                                                          uint32_t changedCapacity,
                                                          uint32_t* changedTotalOut);

#ifdef __cplusplus
}
#endif

#endif
