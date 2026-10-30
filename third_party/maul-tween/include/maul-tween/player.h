// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Players: what
// writes targets. A player plays a sealed clip on targets bound to its
// slots, by Web Animations' procedures on the host's clock; a transition
// is a player too. A player not kept ends by committing its last value
// into its targets' base values and is then freed, its id stale.

#ifndef MAUL_TWEEN_PLAYER_H
#define MAUL_TWEEN_PLAYER_H

#include "maul-tween/base.h"
#include "maul-tween/clip.h"
#include "maul-tween/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A player, by a generation-checked id; zero is null.
    typedef struct mtwnPlayerId
    {
        uint32_t index1;
        uint32_t generation;
    } mtwnPlayerId;

    // How a player's value goes onto what lies under it (Web Animations'
    // composite operations; accumulate is addition for every value kind).
    typedef uint8_t mtwnComposite;
    enum
    {
        // The player's value, mixed toward by its weight.
        mtwn_compositeReplace = 0,
        // The value under it plus the player's, times its weight.
        mtwn_compositeAdd = 1,
    };

    // A slot of a clip bound to a target, with how it composes there.
    // Start from mtwnMakeBinding.
    typedef struct mtwnBinding
    {
        // A slot of the clip, bound by one binding of the player at most.
        uint32_t slot;
        mtwnComposite composite;
        // A live target of the value kind the slot's tracks hold.
        mtwnTargetId target;
        // In [0, 1]: how far from what lies under it toward the composited
        // value.
        float weight;
    } mtwnBinding;

    /// Makes a binding of a slot to a target: replace, at full weight.
    ///
    /// @param slot    The clip's slot.
    /// @param target  The target.
    /// @return The binding.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mtwnBinding mtwnMakeBinding(uint32_t slot, mtwnTargetId target)
    {
#ifdef __cplusplus
        mtwnBinding binding = {};
#else
    mtwnBinding binding = {0};
#endif
        binding.slot = slot;
        binding.target = target;
        binding.weight = 1.0f;
        return binding;
    }

    // How a player is made. Start from mtwnDefaultPlayerDef.
    typedef struct mtwnPlayerDef
    {
        // Set by mtwnDefaultPlayerDef; checked by mtwnCreatePlayer.
        uint32_t cookie;
        // A sealed clip.
        mtwnClipId clip;
        // The number of bindings.
        uint32_t bindingCount;
        // Its slots' targets, copied; each slot at most once and used by the
        // clip's tracks, each target alive and of the slot's kind, each
        // composite known and weight in [0, 1]; a slot left out plays on
        // nothing.
        const mtwnBinding* bindings;
        // Whether it plays at once, starting at the next update.
        bool play;
        // Whether it is kept when it finishes; otherwise it commits what it
        // fills forward as its targets' bases and is freed.
        bool keep;
        // Whether it plays on under reduced motion: a
        // fade, a color, or motion essential to what it shows; the others
        // finish at once if their end is finite.
        bool essential;
        // The playback rate: 0, which holds, or from MTWN_MIN_RATE to
        // MTWN_RATE_LIMIT in size; negative plays backwards.
        double rate;
        // In [0, 1]: scales every binding's weight; mtwnPlayer_SetWeight
        // changes it while it plays. A float, as a binding's weight and
        // the values it blends are.
        float weight;
    } mtwnPlayerDef;

// The cookie mtwnDefaultPlayerDef sets and mtwnCreatePlayer checks.
#define MTWN_PLAYER_DEF_COOKIE 0x59414C50u

// The largest size of a clip's player's rate, 2^20. A rate change keeps
// the current time as Web Animations does, by the start time, so to
// within an ulp of the host time times the rate: at this limit and 10^7 s
// of host time (116 days), 2 ms. A rate past it is refused as misuse.
#define MTWN_RATE_LIMIT 1048576.0

// The smallest size of a clip's player's rate other than 0, 2^-20, the
// inverse of MTWN_RATE_LIMIT. A smaller rate would overflow the start
// time (MTWN_SEEK_LIMIT); one below it is refused as misuse.
#define MTWN_MIN_RATE 0x1p-20

    /// Makes a player def: no clip, no bindings, playing at once at rate 1
    /// and full weight, freed when it ends.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mtwnPlayerDef mtwnDefaultPlayerDef(void)
    {
#ifdef __cplusplus
        mtwnPlayerDef def = {};
#else
    mtwnPlayerDef def = {0};
#endif
        def.cookie = MTWN_PLAYER_DEF_COOKIE;
        def.play = true;
        def.rate = 1.0;
        def.weight = 1.0f;
        return def;
    }

    // A player's play state, Web Animations'.
    typedef uint8_t mtwnPlayState;
    enum
    {
        // No current time: it has no effect.
        mtwn_playIdle = 0,
        // Its current time moves with the host's.
        mtwn_playRunning = 1,
        // Its current time is held.
        mtwn_playPaused = 2,
        // At the end of its playback direction.
        mtwn_playFinished = 3,
    };

    // A player as it is, at the last update's time.
    typedef struct mtwnPlayerState
    {
        // Seconds into the clip; NaN when idle.
        double currentTime;
        double rate;
        // The clip's duration.
        double duration;
        mtwnPlayState state;
        // Whether it waits for the next update to start.
        bool pending;
        // Whether it runs under reduced motion though not essential: its
        // end is infinite or its rate 0, so it could not finish.
        // The host's to keep, seek or stop.
        bool playsOnReduced;
    } mtwnPlayerState;

    /// Makes a player of a sealed clip, which it holds busy until it is
    /// freed; it starts at the next update if the def says play, else idle.
    ///
    /// @param context    The context.
    /// @param def        The def.
    /// @param playerOut  Receives the player's id, or the null id on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null argument, a def
    ///         without its cookie, a rate NaN, a weight outside [0, 1], a
    ///         binding outside the above; mtwn_errorLimit for a rate above
    ///         MTWN_RATE_LIMIT in size, or other than 0 below
    ///         MTWN_MIN_RATE; mtwn_errorStale for a clip that is not
    ///         alive; mtwn_errorState for one not sealed;
    ///         mtwn_errorCapacity when the context holds its capacity of
    ///         players or of bindings.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnCreatePlayer(mtwnContext* context,
                                                        const mtwnPlayerDef* def,
                                                        mtwnPlayerId* playerOut);

    /// Plays a clip's player, as Web Animations plays an animation with
    /// auto-rewind: from the start (or the end, backwards) when idle, at
    /// its end or before its start; a paused player resumes. One that needs
    /// a start time takes the next update's.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @return mtwn_success; mtwn_errorInvalid for a null context;
    ///         mtwn_errorStale for an id that names no live player;
    ///         mtwn_errorInvalid for a transition; mtwn_errorState
    ///         backwards from an end that is infinite.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_Play(mtwnContext* context, mtwnPlayerId player);

    /// Pauses a clip's player at its current time, or at its start (its
    /// end, backwards) when idle.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @return As mtwnPlayer_Play.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_Pause(mtwnContext* context, mtwnPlayerId player);

    /// Reverses a clip's player: negates its rate, keeping its current time,
    /// and plays it with auto-rewind; refused, and nothing changed, when that
    /// would play an infinite clip backwards from its end.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @return As mtwnPlayer_Play.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_Reverse(mtwnContext* context,
                                                          mtwnPlayerId player);

    /// Finishes a clip's player: its current time goes to the end of its
    /// playback direction, the clip's end forwards, 0 backwards, crossing
    /// what lies before it, its records at once.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @return As mtwnPlayer_Play; mtwn_errorState also for a rate of 0 or
    ///         an infinite clip played forwards.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_Finish(mtwnContext* context, mtwnPlayerId player);

    /// Cancels a clip's player: it becomes idle, without effect, and is
    /// kept, to be played again; a cancel record says so if it had a time.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @return As mtwnPlayer_Play.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_Cancel(mtwnContext* context, mtwnPlayerId player);

    /// Seeks a clip's player to a current time, jumping: what lies between
    /// is not crossed, and a seek record says so (mtwnPlayer_Sweep crosses
    /// it). Seeks by progress and label jump too.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @param seconds  The time into the clip, at most MTWN_SEEK_LIMIT in
    ///                 size.
    /// @return As mtwnPlayer_Play; mtwn_errorInvalid also for a NaN time;
    ///         mtwn_errorLimit for one past MTWN_SEEK_LIMIT in size,
    ///         infinity included.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_Seek(mtwnContext* context, mtwnPlayerId player,
                                                       double seconds);

    /// Seeks a clip's player to a time crossing everything between, as an
    /// update sweeping there would: its records come at once, at the last
    /// update's time. A seek otherwise jumps, leaving a seek record.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @param seconds  The time into the clip, at most MTWN_SEEK_LIMIT in
    ///                 size.
    /// @return As mtwnPlayer_Seek.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_Sweep(mtwnContext* context, mtwnPlayerId player,
                                                        double seconds);

    /// Seeks a clip's player to a share of its clip's duration.
    ///
    /// @param context   The context.
    /// @param player    A clip's player.
    /// @param progress  The share, not NaN; the duration must be finite.
    /// @return As mtwnPlayer_Seek, mtwn_errorLimit for a time, the share
    ///         times the duration, past MTWN_SEEK_LIMIT in size;
    ///         mtwn_errorInvalid also for an infinite duration.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_SeekProgress(mtwnContext* context,
                                                               mtwnPlayerId player,
                                                               double progress);

    /// Seeks a clip's player to a label of its clip's root.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @param key      The label's key.
    /// @return As mtwnPlayer_Play; mtwn_errorInvalid also for no such
    ///         label; mtwn_errorLimit for one past MTWN_SEEK_LIMIT in size.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_SeekLabel(mtwnContext* context,
                                                            mtwnPlayerId player, uint32_t key);

    /// Sets a clip's player's rate, keeping its current time: as Web
    /// Animations keeps it, by the start time, so to within an ulp of the
    /// host time times the rate (see MTWN_RATE_LIMIT). A finished player
    /// held at its end keeps its start time too, as Web Animations' steps
    /// say: given a lower rate, the next update finds it short of its end
    /// and it plays again from there, as Firefox does (Chrome keeps a
    /// player that played to its end finished). To stay finished at a new
    /// rate, set the rate before the end.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @param rate     0, which holds, or from MTWN_MIN_RATE to
    ///                 MTWN_RATE_LIMIT in size; negative plays backwards.
    ///                 NaN is refused with mtwn_errorInvalid and another
    ///                 with mtwn_errorLimit, as misuse, the player
    ///                 unchanged.
    /// @return As mtwnPlayer_Play; mtwn_errorInvalid also for a NaN rate;
    ///         mtwn_errorLimit for a rate other than 0 outside
    ///         [MTWN_MIN_RATE, MTWN_RATE_LIMIT] in size.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_SetRate(mtwnContext* context, mtwnPlayerId player,
                                                          double rate);

    /// Sets a clip's player's weight, which scales its bindings' from the
    /// next update: a layer faded in or out.
    ///
    /// @param context  The context.
    /// @param player   A clip's player.
    /// @param weight   In [0, 1].
    /// @return As mtwnPlayer_Play; mtwn_errorInvalid also for a weight
    ///         outside [0, 1].
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_SetWeight(mtwnContext* context,
                                                            mtwnPlayerId player, float weight);

    /// Reads a clip's player's state at the last update's time.
    ///
    /// @param context   The context.
    /// @param player    A clip's player.
    /// @param stateOut  Receives the state, zero on failure.
    /// @return As mtwnPlayer_Play; mtwn_errorInvalid also for a null
    ///         stateOut.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnPlayer_GetState(mtwnContext* context,
                                                           mtwnPlayerId player,
                                                           mtwnPlayerState* stateOut);

    /// Stops a player without committing: its targets show their base
    /// values again at once.
    ///
    /// @param context  The context.
    /// @param player   The player.
    /// @return mtwn_success; mtwn_errorInvalid for a null context;
    ///         mtwn_errorStale for an id that names no live player, as one
    ///         that ended does.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnDestroyPlayer(mtwnContext* context, mtwnPlayerId player);

    /// Tells whether an id names a live player: one that has not ended, been
    /// replaced or been destroyed. Not counted as misuse.
    ///
    /// @param context  The context, or NULL.
    /// @param player   The id.
    /// @return Whether the player is alive.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API bool mtwnPlayer_IsValid(const mtwnContext* context, mtwnPlayerId player);

#ifdef __cplusplus
}
#endif

#endif
