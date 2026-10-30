// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's layout, shared by the modules that work in it.
// The context, its pools and its list of
// changed targets live in one block.

#ifndef MAUL_TWEEN_SRC_CONTEXT_INTERNAL_H
#define MAUL_TWEEN_SRC_CONTEXT_INTERNAL_H

#include "crossing.h"
#include "invariant.h"
#include "pool.h"
#include "runs.h"

#include "maul-tween/clip.h"
#include "maul-tween/context.h"
#include "maul-tween/easing.h"
#include "maul-tween/event.h"
#include "maul-tween/interpolate.h"
#include "maul-tween/target.h"
#include "maul-tween/transition.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// A target: 64 bytes. Its header's link names its transition.
typedef struct mtwnTargetSlot
{
    mtwnSlotHeader header;
    mtwnValueKind kind;
    // The clips' players' bindings naming it, kept as they are made and
    // freed; at 65,535 it stays.
    uint16_t clipWriters;
    mtwnVec4 base;
    mtwnVec4 value;
    mtwnVec4 velocity;
} mtwnTargetSlot;

// The target is on the list of changed targets.
#define MTWN_TARGET_CHANGED 2u
// A clip's player bound to the target has not written it in this update
// yet: if none does, it shows its base.
#define MTWN_TARGET_UNWRITTEN 4u
// The target's velocity is its eased transition's at the last update's
// time, computed when read; its velocity field is
// stale.
#define MTWN_TARGET_LAZY 8u
// The target's velocity is a clip track's, computed when read: its
// velocity field holds an mtwnClipVelocity instead (src/clip_sample.c).
#define MTWN_TARGET_CLIP_LAZY 16u

// What a player is.
#define MTWN_PLAYER_TRANSITION 1u
#define MTWN_PLAYER_CLIP       2u

// A clip's player: Web Animations' times, in seconds, NaN unresolved.
typedef struct mtwnClipPlay
{
    mtwnClipId clip;
    double startTime;
    double holdTime;
    double previousTime;
    double rate;
    // Scales its bindings' weights.
    double weight;
    // A rate waiting for the next update, NaN for none: until then the old
    // rate runs, and from it the new one, the current time kept.
    double pendingRate;
    // The time its events have swept to, NaN for none (idle, or not yet
    // updated since it played): the next sweep starts there.
    double swept;
    // The clips' players before and after it, in no order, 0 for none:
    // the passes over clips' players alone.
    uint32_t previousClip;
    uint32_t nextClip;
    // Its bindings: one target per slot of the clip, from start.
    uint32_t bindingStart;
    uint32_t bindingCount;
    // Waiting for the next update's time to start.
    bool pending;
    bool keep;
    // The way the last sweep went: 1 forwards, -1 backwards, 0 none (or a
    // jump since). A sweep turning back takes its start again.
    int8_t sweptWay;
    // Plays on under reduced motion.
    bool essential;
    // Its place in this update's staging plus 1, 0 for none.
    uint32_t stagedAt;
} mtwnClipPlay;

// A player: a transition or a clip's player. A transition's header link
// names its target.
typedef struct mtwnPlayerSlot
{
    mtwnSlotHeader header;
    uint8_t type;
    // Whether a transition's delay has ended, its start recorded.
    bool begun;
    // Whether the first update has fixed a transition's start time.
    bool started;
    mtwnValueKind kind;
    // The players before and after it in creation order, 0 for none.
    uint32_t earlier;
    uint32_t later;
    union
    {
        mtwnClipPlay play;
        // A transition: what every update reads first, in the slot's first
        // three cache lines with the header, then what it
        // reads when it starts, ends or is replaced. One curve, by its kind.
        struct
        {
            mtwnTransitionKind transitionKind;
            // For an easing that is an integer power already normal, its
            // exponent and mode, so the update reads no more of it;
            // otherwise 0.
            uint8_t power;
            mtwnEaseMode powerMode;
            // Plays on under reduced motion.
            bool essential;
            // A decay's channels (bit i for channel i) that hand over to the
            // bounce at their bound, their end value, and those starting past
            // it; whether it lands, by its channels' own rates.
            uint8_t crosses;
            uint8_t past;
            bool lands;
            uint64_t startNs;
            double delay;
            // Seconds after the delay to the end; infinity for a spring that
            // never settles.
            double endTime;
            double duration;
            mtwnVec4 from;
            // The end value: an angle's moved by its turns, a decay's where
            // it stops.
            mtwnVec4 to;
            union
            {
                mtwnEasing easing;
                mtwnSpring spring;
                mtwnDecay decay;
                // A bounded decay's decay and bounce.
                struct
                {
                    mtwnDecay decay;
                    mtwnSpring bounce;
                } coast;
                // A landing's rate per channel.
                double rates[4];
            };
            mtwnTargetId target;
            union
            {
                // For a spring or decay: the start velocity per channel;
                // for a color or rotation, in progress in the first.
                double velocity[4];
                // For an easing: CSS Transitions' reversing-adjusted start
                // value and reversing shortening factor.
                struct
                {
                    mtwnVec4 reversingStart;
                    double shortening;
                };
            };
            union
            {
                mtwnColorMix color;
                mtwnRotationMix rotation;
                mtwnScaleMix scale;
            } mix;
        };
    };
} mtwnPlayerSlot;

// Whether a kind moves along a path by its progress: colors, rotations and
// scales.
static inline bool mtwnIsPathKind(mtwnValueKind kind)
{
    return kind == mtwn_valueColor || kind == mtwn_valueRotation || kind == mtwn_valueScale;
}

// A node of a clip.
typedef struct mtwnClipNode
{
    // A timeline's with the duration the host set; with autoDuration its
    // children's end stands for it.
    mtwnTiming timing;
    mtwnTrack track;
    // Its start and end in its parent.
    double start;
    double end;
    // For a timeline: the latest end of its children so far.
    double childrenEnd;
    // Its active duration, from its timing as finished, for its events,
    // and its iterations at its active start and end, each the same at
    // every crossing there.
    double activeDuration;
    double startIteration;
    double endIteration;
    // For a timeline: its first and last children, and the last track or
    // timeline among them (positions' previous child); 0 when none.
    uint32_t firstChild;
    uint32_t lastChild;
    uint32_t previous;
    // The next child of its parent, 0 for the last.
    uint32_t nextSibling;
    uint32_t parent;
    uint32_t slot;
    uint32_t key;
    mtwnNodeKind kind;
    bool autoDuration;
    // Timelines above it below the root; the root's is 0.
    uint8_t depth;
    // A track's iterations move on each, a rotation's
    // by step: the rotation from its first keyframe to its last.
    bool incremental;
    // For a track: the node before it is a track of the same parent,
    // start and timing, so its timing results are that track's.
    bool sharesTiming;
    mtwnVec4 step;
} mtwnClipNode;

// A clip, in the context's pool; its storage is a block of its own.
// A node's time during a sample: the time it gives its children now and
// a moment ago (for the velocity), and whether time runs backwards in it;
// during a sweep of events, at the sweep's end and start.
typedef struct mtwnNodeTime
{
    double now;
    double before;
    bool backwards;
    // For events: whether it and every timeline above it stayed in one
    // iteration over the sweep, so its child time moved one way.
    bool steady;
    // For events: whether, besides, every one's child time at both ends lay
    // clearly inside an iteration, off its boundaries, where a crossing's
    // position is one child time.
    bool sure;
    // For events: whether the sweep's start is open (the sweep before ended
    // there) and every one's time there lay inside its active interval,
    // off a repeat delay: moving from there, held nowhere.
    bool openStart;
    // For events: whether one's time at either end of the sweep lay near
    // where a hold meets the time's motion (an active start or end, an
    // iteration's start): held at both ends, it may still have crossed what
    // lies at the held time.
    bool atEdge;
} mtwnNodeTime;

typedef struct mtwnClipSlot
{
    mtwnSlotHeader header;
    bool sealed;
    mtwnClipDef def;
    void* block;
    uint64_t blockBytes;
    // The root is nodes[0].
    mtwnClipNode* nodes;
    mtwnKeyframe* keyframes;
    // The points of the linear() easings of its timings and keyframes,
    // copied in, which those easings reference.
    mtwnLinearPoint* points;
    double* lengths;
    mtwnSegmentMix* mixes;
    // Each slot's value kind plus 1, 0 for a slot no track uses yet.
    uint8_t* slotKinds;
    // Scratch for sampling, one per node.
    mtwnNodeTime* times;
    uint32_t nodeCount;
    uint32_t keyframeCount;
    uint32_t pointCount;
    // The slots its tracks use: the largest plus 1.
    uint32_t slotCount;
    // The players playing it.
    uint32_t users;
} mtwnClipSlot;

// A clip's player's scratch and staging (below).
typedef struct mtwnWork mtwnWork;
typedef struct mtwnStaged mtwnStaged;

struct mtwnContext
{
    mtwnAllocator allocator;
    mtwnContextDef def;
    // The block holding the context and everything in it, and its size.
    uint64_t blockBytes;
    // Counted by mtwnCountMisuse: atomic, as a refusal may come from a task
    // on the host's worker while the update's thread waits.
    _Atomic uint64_t misuse;
    // Clip players' advances a task system's groups left to the serial
    // pass (mtwnContext_GetSerialAdvances).
    uint64_t serialAdvances;
    uint64_t lastTimeNs;
    // Set while one of the context's own functions runs (mtwnEnter), so the
    // host's code it runs (a host easing, the allocator, the task system)
    // calling back in is refused.
    bool busy;
    mtwnPool targets;
    mtwnPool players;
    mtwnPool clips;
    // Each player's def.pointsPerTransition points, in index order: a
    // transition's linear() easing copied in, which its easing references.
    mtwnLinearPoint* transitionPoints;
    // The players in creation order, first and last; 0 for none.
    uint32_t firstPlayer;
    uint32_t lastPlayer;
    // The first of the clips' players, 0 for none.
    uint32_t firstClipPlayer;
    // The clips' players' part of an update, set by the first clip's
    // player made, NULL before. Never cleared: once a
    // clip's player was made its part runs, over an empty chain when the
    // last is gone.
    const struct mtwnClipUpdate* clipUpdate;
    // The players' bindings.
    mtwnRuns bindings;

    // The event queue: a ring of def.eventCapacity records, the oldest at
    // head; and those dropped at a full queue.
    mtwnEvent* events;
    // Beside each record, its place along its sweep, which orders one
    // player's records of one time.
    double* eventAlong;
    uint32_t eventHead;
    uint32_t eventCount;
    uint64_t droppedEvents;
    // Those dropped since the last read.
    uint64_t droppedSinceRead;
    // Scratch for counting crossings: a chain of timelines and the node.
    mtwnLevel* levels;
    // The serial work: those levels, each clip's own node times, no staging.
    mtwnWork* serial;
    // With the host's task system: each group's scratch
    // and staging, and the clips' players in creation order as the last
    // update's parallel phase left them, the serial phase at 'next'; NULL
    // without, or 0 players when the phase did not run.
    mtwnWork* works;
    mtwnStaged* staged;
    uint32_t stagedCount;
    uint32_t stagedNext;
    // The bytes the live clips' blocks hold.
    uint64_t clipStorageUsed;
    // The index1 of each target changed since the last report, in order.
    uint32_t* changed;
    uint32_t changedCount;
    // Whether motion is reduced, and the clips' players
    // the last update left running under it though not essential.
    bool reducedMotion;
    // The live targets whose writers need an order (mtwnLayered): none, and
    // with groups the serial pass marks no target unwritten.
    uint32_t layeredTargets;
    // Advanced by every change to who writes a target (a count, a
    // transition, a target destroyed); the groups work out their
    // bindings' alone again only when it moved since they last did.
    uint64_t writersEpoch;
    uint64_t aloneEpoch;
    bool aloneFresh;
    // For tests: every record counted, none left out as certainly missed
    // or settled, to check that leaving them out changes nothing.
    bool countEvery;
    uint32_t playingOn;
};

// Whether two doubles are the same bits: a key's inputs, where -0 and +0
// (and a rate's sign) differ and NaN is one value.
static inline bool mtwnSameBits(double a, double b)
{
    uint64_t x = 0;
    uint64_t y = 0;
    memcpy(&x, &a, sizeof x);
    memcpy(&y, &b, sizeof y);
    return x == y;
}

// Counts one misuse; relaxed, its only reader the count's getter.
static inline void mtwnCountMisuse(mtwnContext* context)
{
    atomic_fetch_add_explicit(&context->misuse, 1, memory_order_relaxed);
}

// Marks a context busy for one of its functions: false, with one misuse
// counted, when one is running already, the host's code it runs calling
// back in. A NULL context passes, for the call to refuse.
static inline bool mtwnEnter(mtwnContext* context)
{
    if (context == NULL)
    {
        return true;
    }
    if (context->busy)
    {
        mtwnCountMisuse(context);
        return false;
    }
    context->busy = true;
    return true;
}

// Ends what mtwnEnter began.
static inline void mtwnLeave(mtwnContext* context)
{
    if (context != NULL)
    {
        context->busy = false;
    }
}

static inline mtwnTargetSlot* mtwnTargetAt(const mtwnContext* context, uint32_t index1)
{
    return (mtwnTargetSlot*)mtwnPoolAt(&context->targets, index1);
}

static inline mtwnClipSlot* mtwnClipAt(const mtwnContext* context, uint32_t index1)
{
    return (mtwnClipSlot*)mtwnPoolAt(&context->clips, index1);
}

static inline mtwnPlayerSlot* mtwnPlayerAt(const mtwnContext* context, uint32_t index1)
{
    return (mtwnPlayerSlot*)mtwnPoolAt(&context->players, index1);
}

// The points a player's transition copies its linear() easing to.
static inline mtwnLinearPoint* mtwnTransitionPointsAt(const mtwnContext* context, uint32_t index1)
{
    return context->transitionPoints + (size_t)(index1 - 1) * context->def.pointsPerTransition;
}

// A player's id from its slot.
static inline mtwnPlayerId mtwnPlayerIdAt(const mtwnContext* context, uint32_t index1)
{
    return (mtwnPlayerId){index1, mtwnPlayerAt(context, index1)->header.generation};
}

// The channels a kind uses.
static inline uint8_t mtwnKindChannels(mtwnValueKind kind)
{
    static const uint8_t channels[] = {1, 2, 3, 4, 4, 1, 4, 1, 1, 1};
    return channels[kind];
}

// Puts a target on the list of changed targets, once.
static inline void mtwnMarkChanged(mtwnContext* context, uint32_t index1)
{
    mtwnTargetSlot* slot = mtwnTargetAt(context, index1);
    if ((slot->header.flags & MTWN_TARGET_CHANGED) == 0)
    {
        // Each target is on the list once, so it never holds more than
        // the targets.
        MTWN_ASSERT(context->changedCount < context->targets.capacity);
        slot->header.flags |= MTWN_TARGET_CHANGED;
        context->changed[context->changedCount++] = index1;
    }
}

// The value with the kind's channels kept and the others 0, or false when
// the kind refuses it: a channel not finite, or a rotation all zero.
bool mtwnAcceptValue(mtwnValueKind kind, mtwnVec4 value, mtwnVec4* out);

// Takes a player slot, put last in creation order; 0 when the pool is
// full.
uint32_t mtwnTakePlayer(mtwnContext* context, uint8_t type);

// Frees a player: out of creation order, its bindings given back, its
// clip no longer busy for it, its slot returned.
void mtwnFreePlayer(mtwnContext* context, uint32_t player);

// Ends a target's transition: frees the player and unlinks the target.
void mtwnEndTransition(mtwnContext* context, uint32_t player);

// The clips' players' part of an update (src/player.c), reached only
// through the context, so that a host that makes no clip's player links
// none of the players, clip sampling, composition, paths or crossings.
typedef struct mtwnClipUpdate
{
    // Before the players: their targets marked unwritten, and with the
    // host's task system the parallel phase run.
    void (*begin)(mtwnContext* context, double before, double now);
    // A clip's player advanced from the last update's host time to this
    // one's, what it crossed recorded and it sampled; freed when it has
    // finished and is not kept.
    void (*advance)(mtwnContext* context, uint32_t player, double before, double now);
    // After them: a target no clip's player wrote shows its base.
    void (*end)(mtwnContext* context);
    // A target's velocity left to be computed when read by a clip's track.
    mtwnVec4 (*velocity)(const mtwnContext* context, const mtwnTargetSlot* target);
} mtwnClipUpdate;

// Writes a target's value and velocity, marking it changed when its value
// changed and written in this update (src/update.c).
void mtwnWriteTarget(mtwnContext* context, uint32_t index1, mtwnVec4 value, mtwnVec4 velocity);

// A target's velocity: its own, or for a lazy one its transition's at the
// last update's time, in the library's floating-point environment
// (src/update.c).
mtwnVec4 mtwnTargetVelocity(const mtwnContext* context, uint32_t index1);

// An eased transition's progress through its easing at the last update's
// time: 0 before it starts (src/update.c).
double mtwnEasedProgress(const mtwnContext* context, uint32_t player);

// Stores a lazy target's velocity in it, before its transition goes, its
// value changes or its clip goes.
void mtwnSettleVelocity(mtwnContext* context, uint32_t index1);

// Stores the velocities of the targets left lazy by a clip's tracks, before
// the clip goes.
void mtwnSettleClipVelocities(mtwnContext* context, uint32_t clip);

// A target's velocity left to be computed by a clip's track
// (src/clip_sample.c).
mtwnVec4 mtwnClipTrackVelocity(const mtwnContext* context, const mtwnTargetSlot* target);

// The live target a player's binding names, 0 for none.
uint32_t mtwnBoundTarget(const mtwnContext* context, const mtwnClipPlay* play, uint32_t slot);

// Appends a record, with its place along its sweep, dropping it at a full
// queue (src/event.c).
void mtwnPushEvent(mtwnContext* context, const mtwnEvent* event, double along);

// Sorts the records appended since the queue held count, by time, then a
// player's by their place along its sweep, keeping the order made.
void mtwnSortEvents(mtwnContext* context, uint32_t count);

// Records what a clip's player's time crossed sweeping from its swept
// time to 'to' at a rate, the host's time running from hostFrom to hostTo;
// a sweep from no time takes 'to' itself.
void mtwnSweepEvents(mtwnContext* context, mtwnWork* work, uint32_t player, double to, double rate,
                     double hostFrom, double hostTo);
// The same for a player near the last the work advanced: one in step with
// the last noted takes its records, the second in step keeps its own.
void mtwnSweepEventsNear(mtwnContext* context, mtwnWork* work, uint32_t player, double to,
                         double rate, double hostFrom, double hostTo);

// Records a player's event of the player itself: a seek, a cancel, a
// transition's start or end.
void mtwnPlayerEvent(mtwnContext* context, uint32_t player, mtwnEventKind kind, double hostTime,
                     double clipTime);

// Samples a clip's player's tree at its current time into its targets:
// rate is how fast its time moves (0 when held), backwards its playback
// direction; committing writes the values as the bases too
// (src/clip_sample.c).
void mtwnSampleClip(mtwnContext* context, mtwnWork* work, const mtwnClipPlay* play, double current,
                    double rate, bool backwards, bool commit);
// The same for a player near the last the work advanced, as
// mtwnSweepEventsNear: its track results taken or kept.
void mtwnSampleClipNear(mtwnContext* context, mtwnWork* work, const mtwnClipPlay* play,
                        double current, double rate, bool backwards, bool commit);

// The groups of clips' players the parallel phase hands the host, and the
// node times each group keeps: a clip of more nodes plays serially.
#define MTWN_GROUPS      16u
#define MTWN_GROUP_NODES 128u
// The most event records a group stages.
#define MTWN_GROUP_RECORDS 1024u

// A clip track's result for its target before it is composed there: its
// own value and velocity, or, left lazy, its lazy velocity's bytes
// (src/clip_sample.c).
typedef struct mtwnTrackOut
{
    mtwnVec4 value;
    mtwnVec4 velocity;
    double weight;
    uint32_t target;
    mtwnValueKind kind;
    mtwnComposite composite;
    bool lazy;
    bool commit;
    // Staged: how many changed targets its group had staged before it, so
    // the serial pass keeps their order with the results'.
    uint32_t changesBefore;
} mtwnTrackOut;

// A clip track's result as the last player of its clip in a work made it,
// for the next player in step: its own value and
// timing results, its parent's time a moment ago and way, and its
// velocity once worked out.
typedef struct mtwnSharedTrack
{
    mtwnVec4 value;
    mtwnVec4 velocity;
    double progress;
    double iteration;
    double earlier;
    double earlierIteration;
    double parentBefore;
    bool earlierKnown;
    bool parentBackwards;
    // Worked out for the last player; with an effect; velocity known.
    bool known;
    bool effect;
    bool velocityKnown;
} mtwnSharedTrack;

// A clip's player's records as the last player of its clip in a work made
// them, for the next one sweeping the same way.
typedef struct mtwnSharedRecord
{
    mtwnEvent event;
    double along;
} mtwnSharedRecord;

// What the last player in a work left for the next in step: the sample's
// key and its tracks, and the sweep's key and its records. A player only
// notes its key; the next in step with it keeps its results, for the rest
// to take, so players out of step keep nothing. A clip of more nodes, or a
// sweep of more records, is not shared.
typedef struct mtwnSharing
{
    mtwnSharedTrack* tracks;
    mtwnSharedRecord* records;
    uint32_t trackCapacity;
    uint32_t recordCapacity;
    // The last player's clip by index and its time: a player of both is
    // near it, and only one near checks the keys below.
    uint32_t nearClip;
    double nearCurrent;
    // The sample's key: the clip by index and generation, since a slot is
    // reused by a later clip. Whether a player commits is its own.
    mtwnClipId sampleClip;
    double current;
    double rate;
    bool backwards;
    // Whether a key is noted, and whether the tracks hold its results.
    bool sampleValid;
    bool sampleKept;
    // The sweep's key, noted, and whether the records hold its records.
    bool sweepValid;
    bool sweepKept;
    // Whether the sweep running keeps its records.
    bool capture;
    mtwnClipId sweepClip;
    uint32_t recordCount;
    double from;
    double to;
    double sweepRate;
    double hostFrom;
    double hostTo;
    bool fromNone;
    bool turning;
    bool negative;
} mtwnSharing;

// What a clip's player's advance works in: the crossing levels and node
// times it uses as scratch, and, staging, where its records and track
// results go instead of the queue and the targets. The serial work stages
// nothing and uses the context's levels and each clip's own node times.
typedef struct mtwnWork
{
    mtwnLevel* levels;
    // NULL for each clip's own.
    mtwnNodeTime* times;
    // NULL to apply as made; else staged up to their capacities.
    mtwnEvent* records;
    double* recordAlong;
    mtwnTrackOut* tracks;
    uint32_t recordCapacity;
    uint32_t recordCount;
    uint32_t trackCapacity;
    uint32_t trackCount;
    // Records past a capacity equal to the queue's, which a full queue
    // would drop in the serial order anyway.
    uint64_t dropped;
    // The changed targets staged for its players' alone targets.
    uint32_t* changes;
    uint32_t changeCapacity;
    uint32_t changeCount;
    // Whether staging ran out for the player being advanced.
    bool overflow;
    // What players in step share in this work.
    mtwnSharing sharing;
} mtwnWork;

// A clip's player as the parallel phase left it: its staged records and
// track results in its group's work, ending at the next one's start.
typedef struct mtwnStaged
{
    uint32_t player;
    uint32_t recordStart;
    uint32_t recordEnd;
    uint32_t trackStart;
    uint32_t trackEnd;
    // The changed targets its group staged for its alone targets: from its
    // composing [changeStart, resetStart), from showing their bases
    // [resetStart, changeEnd).
    uint32_t changeStart;
    uint32_t resetStart;
    uint32_t changeEnd;
    uint8_t group;
    // Left whole to the serial phase.
    bool serial;
    // Finished and not kept: freed once applied.
    bool done;
    // Bound to a target others write too.
    bool layered;
} mtwnStaged;

// Writes a track's result into its target, composed onto what lies under
// it there; the target put on the changed list, or for a group's alone
// target staged in the group's work (src/clip_sample.c).
void mtwnApplyTrack(mtwnContext* context, const mtwnTrackOut* out, mtwnWork* group);

// Advances a clip's player from the last update's host time to this one's
// in a work, what it crossed recorded and it sampled: true when it has
// finished and is not kept, to be freed (src/player.c).
bool mtwnAdvanceClipPlayer(mtwnContext* context, mtwnWork* work, uint32_t player, double before,
                           double now);

// The node times a work samples and sweeps a clip in: a group's own, but
// a clip's own when the clip has one player, which no other group can
// then touch; they lie beside its nodes, which the serial update walks
// one after the other, and a group's apart from them took twice the
// cache misses on one worker.
static inline mtwnNodeTime* mtwnTimesFor(const mtwnWork* work, mtwnClipSlot* clip)
{
    return work->times != NULL && clip->users != 1u ? work->times : clip->times;
}

// Whether a target's writers need an order among them: two clips'
// players' bindings or more, or one beside a transition. One binding alone
// is composed in its player's group.
static inline bool mtwnLayered(const mtwnTargetSlot* slot)
{
    return slot->clipWriters >= 2u || (slot->clipWriters == 1u && slot->header.link != 0);
}

// A live target's clips' players' bindings one more or one fewer, the
// layered targets kept; a count at its top stays.
static inline void mtwnAddWriter(mtwnContext* context, mtwnTargetSlot* slot, bool more)
{
    bool was = mtwnLayered(slot);
    ++context->writersEpoch;
    // A binding is taken away only after it was counted.
    MTWN_ASSERT(more || slot->clipWriters > 0);
    if (slot->clipWriters != UINT16_MAX)
    {
        slot->clipWriters = (uint16_t)(more ? slot->clipWriters + 1u : slot->clipWriters - 1u);
    }
    context->layeredTargets =
        context->layeredTargets - (was ? 1u : 0u) + (mtwnLayered(slot) ? 1u : 0u);
}

// A live target's transition set (index1) or ended (0), the layered
// targets kept.
static inline void mtwnLinkTarget(mtwnContext* context, mtwnTargetSlot* slot, uint32_t link)
{
    bool was = mtwnLayered(slot);
    MTWN_ASSERT(!was || context->layeredTargets > 0);
    ++context->writersEpoch;
    slot->header.link = link;
    context->layeredTargets =
        context->layeredTargets - (was ? 1u : 0u) + (mtwnLayered(slot) ? 1u : 0u);
}

// The live targets mtwnLayered counts, recounted: what layeredTargets
// keeps, for an assertion (src/target.c).
uint32_t mtwnCountLayered(const mtwnContext* context);

// The parallel phase: the clips' players in creation order, in groups on
// the host's task system, staged; nothing without one (src/clip_tasks.c).
void mtwnRunClipGroups(mtwnContext* context, double before, double now);

// The serial pass's part for the next clip's player the phase staged: its
// records queued and its results applied, or, left to this pass, advanced
// here; true when it has finished and is not kept.
bool mtwnApplyStaged(mtwnContext* context, uint32_t player, double before, double now);

// After the serial pass: the records the groups dropped counted.
void mtwnEndClipGroups(mtwnContext* context);

// Queues a record, or stages it in a staging work (src/event.c).
void mtwnWorkEvent(mtwnContext* context, mtwnWork* work, const mtwnEvent* event, double along);

// Marks a clip's player's targets unwritten, before the update's players
// run.
void mtwnMarkUnwritten(mtwnContext* context, const mtwnClipPlay* play);

// Marks unwritten those of a clip's player's targets whose binding's alone
// is as given: how many it marked; othersOut, when not NULL, receives how
// many bindings name a target and are of the other kind. Alone, in the
// player's group: first works out each binding's alone if the writers
// changed since the groups last did; an alone target is then alive, so it
// is not looked up.
uint32_t mtwnMarkUnwrittenOf(mtwnContext* context, const mtwnClipPlay* play, bool alone,
                             uint32_t* othersOut);

// Shows their bases in a clip's player's targets no player wrote.
void mtwnResetUnwritten(mtwnContext* context, const mtwnClipPlay* play);

// Shows their bases in a clip's player's alone targets it did not write, in
// its group, their changes staged in the group's work; in a group only,
// its alone worked out there.
void mtwnResetAlone(mtwnContext* context, const mtwnClipPlay* play, mtwnWork* group);

// After the serial pass: the bases shown in the targets of the clips'
// players the groups staged, in the clips' chain's order (src/clip_tasks.c).
void mtwnResetEveryStaged(mtwnContext* context);

// Shows their bases in a clip's player's targets no player wrote, after
// the groups: what its group showed is put on the changed list in the
// bindings' order, the rest shown here (src/clip_tasks.c).
void mtwnResetStaged(mtwnContext* context, const mtwnClipPlay* play);

#endif
