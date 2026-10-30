// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The records a sweep of a clip's player makes.
// Every node's active start and end, iteration starts and
// marks are counted by the crossing engine along the
// chain of timelines above it; a subtree whose timelines all stayed in one
// iteration without their time moving cannot have been crossed and is
// passed over, unless the sweep ends where that time's hold meets its
// motion (an active start or end). Records are pushed in the order they are made (clip order,
// then start, iteration, end); src/event.c sorts them once the update is
// done. Apart from the queue, so that a host with no clip's player links
// none of it.

#include "context_internal.h"
#include "crossing.h"
#include "work.h"

#include <math.h>

// A sweep in progress: its player, its ends, and the host's times.
typedef struct Sweep
{
    mtwnContext* context;
    // Its scratch, and where its records go.
    mtwnWork* work;
    mtwnNodeTime* times;
    uint32_t player;
    const mtwnClipPlay* play;
    double from;
    double to;
    bool fromNone;
    // Whether the sweep takes its start: from no time, or turning back.
    bool fromClosed;
    double hostFrom;
    double hostTo;
    // The rate the player's time ran at since hostFrom.
    double rate;
    // The last crossing found, NaN before any: records often share one
    // (a track's end and the next one's start at a timeline's repeat), so
    // the next search starts there.
    double guess;
    // The records the last track made, for the next one when it shares its
    // timing: its start, iterations and end are then the same leaves on
    // the same chain, so the same records but for the node. replayNode is
    // that track, 0 when none is kept.
    uint32_t replayNode;
    uint32_t replayCount;
    mtwnEvent replay[6];
    double replayAlong[6];
} Sweep;

// Keeps a record for the next player in step while the work captures;
// past the sharing's capacity none of this sweep's are kept.
static void Capture(Sweep* sweep, const mtwnEvent* event, double along)
{
    mtwnSharing* sharing = &sweep->work->sharing;
    if (!sharing->capture)
    {
        return;
    }
    MTWN_ASSERT(sharing->recordCount <= sharing->recordCapacity);
    if (sharing->recordCount == sharing->recordCapacity)
    {
        sharing->capture = false;
        return;
    }
    sharing->records[sharing->recordCount++] = (mtwnSharedRecord){*event, along};
}

// The host's time a player's time was reached at: from the sweep's start
// at the rate that ran since (which the sweep runs with), within the
// sweep; the sweep's end when no time passed or none ran. Within it even
// when the player's time moved otherwise: a rate change keeps the time
// only to the host time's resolution times the rate.
static double HostAt(const Sweep* sweep, double t)
{
    if (sweep->fromNone || sweep->hostTo == sweep->hostFrom || sweep->rate == 0.0)
    {
        return sweep->hostTo;
    }
    double host = sweep->hostFrom + (t - sweep->from) / sweep->rate;

    return host > sweep->hostTo ? sweep->hostTo : (host < sweep->hostFrom ? sweep->hostFrom : host);
}

// What a crossing of a leaf is, either way: the kinds, and whether a kind
// counts at all for the way.
typedef struct Kinds
{
    mtwnEventKind forwards;
    mtwnEventKind backwards;
} Kinds;

// A record's iteration at a crossing: the known one, else the levels'.
static double IterationOf(const mtwnLevel* levels, uint32_t count, double known, double t)
{
    return isnan(known) ? mtwnIterationAt(levels, count, t) : known;
}

// A record's crossings one way, which are never NaN: what a later time
// reaches includes what an earlier one does (src/crossing.c). Were they
// NaN, they would make no record, as converting them to the record's
// count would be undefined.
static double Crossed(double n)
{
    MTWN_ASSERT(!isnan(n));
    return n;
}

// Whether the records kept for a track have room for one more: a track
// makes at most six, its start, iterations and end, each either way.
// Only an assertion calls it.
[[maybe_unused]] static inline bool RoomToReplay(const Sweep* sweep, mtwnNode node)
{
    return sweep->replayNode != node || sweep->replayCount < 6;
}

// Records a leaf's crossings, chain levels [0, count) above it and the
// node whose iteration the records carry at levels[iterationLevels - 1]:
// that iteration when known, the same at every crossing (an active start
// or end), NaN for the levels to be walked.
static void Record(Sweep* sweep, uint32_t count, uint32_t iterationLevels, const mtwnLeaf* leaf,
                   Kinds kinds, mtwnNode node, uint32_t key, double iteration)
{
    mtwnLevel* levels = sweep->work->levels;
    mtwnPrepareLevels(levels, count, leaf);
    // Counted without the outer levels passing their time through.
    uint32_t passed = mtwnPassedLevels(levels, count);
    const mtwnLevel* counted = levels + passed;
    uint32_t below = count - passed;
    mtwnCrossingMemo memo;
    memo.count = 0;
    memo.next = 0;
    mtwnCrossings crossings = mtwnCountCrossingsMemo(counted, below, leaf, sweep->from, sweep->to,
                                                     sweep->fromClosed, &memo);
    if (sweep->fromNone && sweep->play->rate < 0.0)
    {
        // From no time, the point taken is passed the way the player runs.
        crossings = (mtwnCrossings){crossings.opposite, crossings.same};
    }
    for (int way = 0; way < 2; ++way)
    {
        double n = Crossed(way == 0 ? crossings.same : crossings.opposite);
        if (!(n >= 1.0))
        {
            continue;
        }
        mtwnCrossingWay search = sweep->fromNone ? mtwn_crossingEither
                                 : way == 0      ? mtwn_crossingForwards
                                                 : mtwn_crossingBackwards;
        // The last is searched from the first; with one crossing it is
        // the first. Searched apart, the two could land a double from each
        // other, in neighboring iterations.
        double first = mtwnFindCrossingFrom(counted, below, leaf, sweep->from, sweep->to,
                                            sweep->fromClosed, false, search, sweep->guess, &memo);
        double last = n == 1.0
                          ? first
                          : mtwnFindCrossingFrom(counted, below, leaf, sweep->from, sweep->to,
                                                 sweep->fromClosed, true, search, first, &memo);
        sweep->guess = last;
        mtwnEvent event = {0};
        event.hostTime = HostAt(sweep, first);
        event.clipTime = first;
        // One crossing's iteration is looked up once.
        event.firstIteration = IterationOf(levels, iterationLevels, iteration, first);
        event.lastIteration = last == first ? event.firstIteration
                                            : IterationOf(levels, iterationLevels, iteration, last);
        // Exact below 2^64, where a count converts; past it the most there is.
        event.count = n >= 0x1p64 ? UINT64_MAX : (uint64_t)n;
        event.player = mtwnPlayerIdAt(sweep->context, sweep->player);
        event.node = node;
        event.key = key;
        event.kind = way == 0 ? kinds.forwards : kinds.backwards;
        event.backwards = way != 0;
        // Its place along the sweep: how far from the sweep's start.
        double along = fabs(first - sweep->from);
        mtwnWorkEvent(sweep->context, sweep->work, &event, along);
        Capture(sweep, &event, along);
        MTWN_ASSERT(RoomToReplay(sweep, node));
        if (sweep->replayNode == node && sweep->replayCount < 6)
        {
            sweep->replay[sweep->replayCount] = event;
            sweep->replayAlong[sweep->replayCount] = along;
            ++sweep->replayCount;
        }
    }
}

// Fills the chain of timelines above a node, the root first, and the node
// after them; returns the chain's length.
static uint32_t Chain(mtwnLevel* levels, const mtwnClipSlot* clip, uint32_t node)
{
    uint32_t count = 0;
    for (uint32_t at = clip->nodes[node - 1].parent; at != 0; at = clip->nodes[at - 1].parent)
    {
        ++count;
    }
    // A timeline is at most MTWN_MAX_CLIP_DEPTH below the root, and the
    // levels hold its child's chain and the child: MTWN_MAX_CLIP_DEPTH + 2.
    MTWN_ASSERT(count < MTWN_MAX_CLIP_DEPTH + 2u);
    uint32_t i = count;
    for (uint32_t at = node; at != 0; at = clip->nodes[at - 1].parent)
    {
        const mtwnClipNode* n = &clip->nodes[at - 1];
        // Its preparation writes the rest.
        levels[i].timing = n->timing;
        levels[i].start = n->start;
        // Kept when its clip was sealed, for timelines and tracks.
        levels[i].activeDuration = n->activeDuration;
        levels[i].activeKnown = n->kind == mtwn_nodeTimeline || n->kind == mtwn_nodeTrack;
        if (i == 0)
        {
            break;
        }
        --i;
    }
    return count;
}

static mtwnLeaf InstantAt(double at)
{
    mtwnLeaf leaf = {0};
    leaf.at = at;
    leaf.kind = mtwn_leafInstant;
    return leaf;
}

// The margin Missed leaves. The child times it compares are worked out by
// the operations the exact count uses on the same operands (the parent's
// time less the node's start, scaled / d + s and its integer part, the
// easing times the duration), so they are its values bit for bit, and the
// rounding of large operands is common to both (the
// differential agrees at times to 10^15 s). The margin, relative, keeps
// Missed off the exact count's special cases at an iteration's ends.
static const double s_missedMargin = 1e-6;

// Whether the sweep certainly left the parent's child time short of the
// span [from, to] in it: the parent moved one way in one iteration of each
// timeline above (its child time monotonic, timelines' easings being so),
// both ends clearly inside those iterations, so it covered the span
// between its two ends, and a point well outside was not crossed.
// And a point the parent's time stood on at an open start of
// the sweep, moving from there: the sweep before crossed it. False when
// unsure, NaN included.
static bool Missed(const mtwnNodeTime* parent, double from, double to)
{
    if (!parent->sure)
    {
        return false;
    }
    if (parent->openStart && from == to && from == parent->before)
    {
        return true;
    }
    double low = parent->now < parent->before ? parent->now : parent->before;
    double high = parent->now < parent->before ? parent->before : parent->now;
    double below = low - s_missedMargin * (1.0 + fabs(low));
    double above = high + s_missedMargin * (1.0 + fabs(high));
    return to < below || from > above;
}

// Whether a timing has iteration starts after its first: an integer
// strictly inside (iterationStart, iterationStart + iterations).
static bool Iterates(const mtwnTiming* timing)
{
    return floor(timing->iterationStart) + 1.0 < timing->iterationStart + timing->iterations;
}

// Whether a child time lies clearly inside an iteration of a duration:
// off 0 and the duration by more than the margin Missed leaves. False for
// NaN.
static bool Inside(double child, double duration)
{
    double margin = s_missedMargin * (1.0 + fabs(child));
    bool offStart = fabs(child) > margin;
    bool offEnd = isinf(duration) || fabs(child - duration) > margin;
    return offStart && offEnd;
}

// Whether a lies within the margin Missed leaves of b.
static bool Near(double a, double b)
{
    return fabs(a - b) <= s_missedMargin * (1.0 + fabs(b));
}

// A timeline's child times at the sweep's ends, whether it stayed in one
// iteration, and whether it moved at the sweep's start.
static void Times(const mtwnClipNode* n, const mtwnNodeTime* parent, mtwnNodeTime* out)
{
    bool movingAtStart = false;
    bool atEdge = parent->atEdge;
    mtwnTiming timing = n->timing;
    timing.fill = mtwn_fillBoth;
    double activeStart = n->start + n->timing.delay;
    double ends[2] = {parent->now, parent->before};
    double iterations[2] = {0.0, 0.0};
    double children[2] = {0.0, 0.0};
    for (int k = 0; k < 2; ++k)
    {
        mtwnTimingResult result;
        mtwnComputeTimingWork(&timing, ends[k] - n->start, false, &result);
        iterations[k] = result.iteration;
        children[k] = isinf(timing.duration)
                          ? (result.forwards ? result.activeTime * timing.playbackRate : HUGE_VAL)
                          : result.transformedProgress * timing.duration;
        // Near where a hold meets its time's motion: its active start or
        // end, an iteration's start (also the end of the one before, and
        // of a repeat delay), within the margin Missed leaves, as the
        // count's own times may fall a double either side.
        atEdge = atEdge || Near(ends[k], activeStart) ||
                 Near(ends[k], activeStart + n->activeDuration) ||
                 (result.phase == mtwn_phaseActive && !Inside(children[k], timing.duration));
        if (k == 1)
        {
            // Inside its active interval, off its ends and a repeat delay:
            // not held. At the active start itself the count may reach the
            // start a double later (rounding through the levels above).
            movingAtStart = result.phase == mtwn_phaseActive && result.activeTime > 0.0 &&
                            result.activeTime < result.activeDuration && !result.inRepeatDelay;
        }
    }
    // A point interval passed within the sweep holds its time either side
    // but is crossed: its boundary lies between the ends, not near them.
    double low = ends[0] < ends[1] ? ends[0] : ends[1];
    double high = ends[0] < ends[1] ? ends[1] : ends[0];
    double activeEnd = activeStart + n->activeDuration;
    atEdge = atEdge || (low < activeStart && activeStart < high) ||
             (low < activeEnd && activeEnd < high);
    out->now = children[0];
    out->before = children[1];
    out->steady = parent->steady && iterations[0] == iterations[1];
    out->openStart = parent->openStart && movingAtStart;
    out->atEdge = atEdge;
    // Its time at the sweep's start may lie on an iteration's start or end
    // when that start is open and it moves from there: the sweep before
    // crossed the boundary, and this one's crossings are one child time
    // each as inside.
    bool fromBoundary = out->openStart && (children[1] == 0.0 || children[1] == timing.duration);
    out->sure = parent->sure && out->steady && Inside(children[0], timing.duration) &&
                (Inside(children[1], timing.duration) || fromBoundary);
}

// Whether no iteration of a node started over the sweep: its parent sure,
// its own time stayed in one iteration, clearly inside it, so it moved
// one way between two points of that iteration (the shortcut's own
// argument, one level down). A timeline's times are worked out already; a
// track's are worked out here.
static bool Settled(const mtwnClipSlot* clip, const mtwnNodeTime* times, uint32_t node,
                    const mtwnNodeTime* parent)
{
    if (!parent->sure)
    {
        return false;
    }
    const mtwnClipNode* n = &clip->nodes[node - 1];
    if (n->kind == mtwn_nodeTimeline)
    {
        return times[node - 1].sure;
    }
    mtwnNodeTime own;
    Times(n, parent, &own);
    return own.sure;
}

// Where in the sweep the parent's child time likely reached x, as the next
// search's guess: the sweep's ends interpolated, the parent having moved
// one way in one iteration. Kept as the guess unless the parent stood still
// or did not stay in one iteration; a wrong guess only narrows a search.
static void GuessAt(Sweep* sweep, const mtwnNodeTime* parent, double x)
{
    double moved = parent->now - parent->before;
    if (!parent->steady || moved == 0.0 || !isfinite(x))
    {
        return;
    }
    double guess = sweep->from + (x - parent->before) / moved * (sweep->to - sweep->from);
    if (isfinite(guess))
    {
        sweep->guess = guess;
    }
}

// Where in its parent a node's iteration starts between the sweep's ends,
// roughly (a repeat delay and a negative rate not reckoned): NaN for none.
static double IterationStartIn(const mtwnClipNode* n, const mtwnNodeTime* parent)
{
    const mtwnTiming* t = &n->timing;
    double rate = fabs(t->playbackRate);
    if (!(t->duration > 0.0) || isinf(t->duration))
    {
        return (double)NAN;
    }
    double a = ((parent->before - n->start) - t->delay) * rate / t->duration + t->iterationStart;
    double b = ((parent->now - n->start) - t->delay) * rate / t->duration + t->iterationStart;
    double k = floor(a > b ? a : b);
    if (!(k > (a < b ? a : b)))
    {
        return (double)NAN;
    }
    return n->start + t->delay + (k - t->iterationStart) * t->duration / rate;
}

// A node's records: a mark's crossings, or a track's or timeline's start,
// iterations and end. Those its parent certainly did not reach are not
// counted, their counts being 0, nor its iterations when it settled in
// one.
static void NodeEvents(Sweep* sweep, const mtwnClipSlot* clip, uint32_t node,
                       const mtwnNodeTime* parent)
{
    const mtwnClipNode* n = &clip->nodes[node - 1];
    mtwnLevel* levels = sweep->work->levels;
    if (n->kind == mtwn_nodeMark)
    {
        if (!Missed(parent, n->start, n->start))
        {
            mtwnLeaf mark = InstantAt(n->start);
            GuessAt(sweep, parent, n->start);
            uint32_t count = Chain(levels, clip, node);
            Record(sweep, count, count, &mark, (Kinds){mtwn_eventMark, mtwn_eventMark}, node,
                   n->key, (double)NAN);
        }
        return;
    }
    if (n->kind == mtwn_nodeLabel)
    {
        return;
    }
    if (n->kind == mtwn_nodeTrack && n->sharesTiming && sweep->replayNode == node - 1)
    {
        for (uint32_t i = 0; i < sweep->replayCount; ++i)
        {
            mtwnEvent event = sweep->replay[i];
            event.node = node;
            mtwnWorkEvent(sweep->context, sweep->work, &event, sweep->replayAlong[i]);
            Capture(sweep, &event, sweep->replayAlong[i]);
        }
        sweep->replayNode = node;
        return;
    }
    sweep->replayNode = n->kind == mtwn_nodeTrack ? node : 0;
    sweep->replayCount = 0;
    double activeStart = n->start + n->timing.delay;
    double activeEnd = activeStart + n->activeDuration;
    // The levels below the chain hold the node itself, for its iteration;
    // preparing a leaf leaves them be, so the chain is filled again for
    // each. A leaf, with its timing, is made only when it is counted.
    if (!Missed(parent, activeStart, activeStart))
    {
        mtwnLeaf start = InstantAt(activeStart);
        GuessAt(sweep, parent, activeStart);
        uint32_t count = Chain(levels, clip, node);
        Record(sweep, count, count + 1, &start, (Kinds){mtwn_eventStart, mtwn_eventEnd}, node, 0,
               n->startIteration);
    }
    if (Iterates(&n->timing) && !Missed(parent, activeStart, activeEnd) &&
        !Settled(clip, sweep->times, node, parent))
    {
        mtwnLeaf iterations = {0};
        iterations.at = n->start;
        iterations.timing = n->timing;
        iterations.kind = mtwn_leafIterations;
        iterations.activeDuration = n->activeDuration;
        iterations.activeKnown = true;
        GuessAt(sweep, parent, IterationStartIn(n, parent));
        uint32_t count = Chain(levels, clip, node);
        Record(sweep, count, count + 1, &iterations,
               (Kinds){mtwn_eventIteration, mtwn_eventIteration}, node, 0, (double)NAN);
    }
    // An endless node's end lies at no time: not even a reversed endless
    // timeline's children, at infinity, reach it.
    if (isfinite(activeEnd) && !Missed(parent, activeEnd, activeEnd))
    {
        mtwnLeaf end = InstantAt(activeEnd);
        GuessAt(sweep, parent, activeEnd);
        uint32_t count = Chain(levels, clip, node);
        Record(sweep, count, count + 1, &end, (Kinds){mtwn_eventEnd, mtwn_eventStart}, node, 0,
               n->endIteration);
    }
}

// Whether a player's clip is the one it plays, sealed, every node after
// its parent, a timeline, so the sweep has its parent's times when it
// comes to it. A player's clip is sealed before it plays and kept while
// it does: destroying it is refused as busy. Only an assertion calls it.
[[maybe_unused]] static inline bool Playable(const mtwnClipSlot* clip, const mtwnClipPlay* play)
{
    if (clip->header.generation != play->clip.generation || !clip->sealed ||
        clip->nodes[0].parent != 0)
    {
        return false;
    }
    for (uint32_t i = 1; i < clip->nodeCount; ++i)
    {
        uint32_t parent = clip->nodes[i].parent;
        if (parent == 0 || parent > i || clip->nodes[parent - 1].kind != mtwn_nodeTimeline)
        {
            return false;
        }
    }
    return true;
}

// The sweep's key: what decides its records but the player.
typedef struct SweepKey
{
    mtwnClipId clip;
    double from;
    double to;
    double rate;
    double hostFrom;
    double hostTo;
    bool fromNone;
    bool turning;
    bool negative;
} SweepKey;

static bool SameSweep(const mtwnSharing* sharing, const SweepKey* key)
{
    return sharing->sweepValid && sharing->sweepClip.index1 == key->clip.index1 &&
           sharing->sweepClip.generation == key->clip.generation &&
           sharing->fromNone == key->fromNone &&
           (key->fromNone || mtwnSameBits(sharing->from, key->from)) &&
           mtwnSameBits(sharing->to, key->to) && mtwnSameBits(sharing->sweepRate, key->rate) &&
           mtwnSameBits(sharing->hostFrom, key->hostFrom) &&
           mtwnSameBits(sharing->hostTo, key->hostTo) && sharing->turning == key->turning &&
           sharing->negative == key->negative;
}

static void KeepSweep(mtwnSharing* sharing, const SweepKey* key)
{
    sharing->sweepClip = key->clip;
    sharing->from = key->from;
    sharing->to = key->to;
    sharing->sweepRate = key->rate;
    sharing->hostFrom = key->hostFrom;
    sharing->hostTo = key->hostTo;
    sharing->fromNone = key->fromNone;
    sharing->turning = key->turning;
    sharing->negative = key->negative;
    sharing->recordCount = 0;
    sharing->sweepValid = true;
    sharing->sweepKept = false;
}

void mtwnSweepEvents(mtwnContext* context, mtwnWork* work, uint32_t player, double to, double rate,
                     double hostFrom, double hostTo)
{
    mtwnClipPlay* play = &mtwnPlayerAt(context, player)->play;
    double from = play->swept;
    bool fromNone = isnan(from);
    play->swept = to;
    if (context->def.eventCapacity == 0)
    {
        // No records to make: nothing to sweep, the time kept so that a
        // context grown to hold records sweeps on from here.
        play->sweptWay = 0;
        return;
    }
    if (!fromNone && from == to)
    {
        return;
    }
    int8_t way = fromNone ? (int8_t)(play->rate < 0.0 ? -1 : 1) : (int8_t)(to > from ? 1 : -1);
    bool turning = !fromNone && play->sweptWay != 0 && play->sweptWay != way;
    play->sweptWay = way;
    mtwnClipSlot* clip = mtwnClipAt(context, play->clip.index1);
    MTWN_ASSERT(Playable(clip, play));
    // Set field by field: the kept records need no clearing.
    mtwnNodeTime* times = mtwnTimesFor(work, clip);
    Sweep sweep;
    sweep.context = context;
    sweep.work = work;
    sweep.times = times;
    sweep.player = player;
    sweep.play = play;
    sweep.from = fromNone ? to : from;
    sweep.to = to;
    sweep.fromNone = fromNone;
    sweep.fromClosed = fromNone || turning;
    sweep.hostFrom = hostFrom;
    sweep.hostTo = hostTo;
    sweep.rate = rate;
    sweep.guess = (double)NAN;
    sweep.replayNode = 0;
    sweep.replayCount = 0;
    // The player's time is the root's parent's, and moves one way; unsure
    // when a test asks every record counted, so nothing is left out.
    mtwnNodeTime top = {to,   sweep.from, false, true, !context->countEvery, !sweep.fromClosed,
                        false};
    for (uint32_t i = 0; i < clip->nodeCount; ++i)
    {
        const mtwnClipNode* n = &clip->nodes[i];
        const mtwnNodeTime* parent = i == 0 ? &top : &times[n->parent - 1];
        // Held over the sweep, and not on a hold's edge at its end, where a
        // point at the held time is crossed.
        bool still =
            !fromNone && parent->steady && parent->now == parent->before && !parent->atEdge;
        if (n->kind == mtwn_nodeTimeline)
        {
            if (still)
            {
                times[i] = (mtwnNodeTime){0.0, 0.0, false, true, true, false, false};
            }
            else
            {
                Times(n, parent, &times[i]);
            }
        }
        if (!still)
        {
            NodeEvents(&sweep, clip, i + 1, parent);
        }
    }
}

// A sweep's key, the player's swept time and way left as they are: false
// when its time did not move.
static bool KeyOf(const mtwnClipPlay* play, double to, double rate, double hostFrom, double hostTo,
                  SweepKey* key, int8_t* way)
{
    double from = play->swept;
    bool fromNone = isnan(from);
    if (!fromNone && from == to)
    {
        return false;
    }
    *way = fromNone ? (int8_t)(play->rate < 0.0 ? -1 : 1) : (int8_t)(to > from ? 1 : -1);
    bool turning = !fromNone && play->sweptWay != 0 && play->sweptWay != *way;
    *key = (SweepKey){play->clip,      from, to, rate, hostFrom, hostTo, fromNone, turning,
                      play->rate < 0.0};
    return true;
}

void mtwnSweepEventsNear(mtwnContext* context, mtwnWork* work, uint32_t player, double to,
                         double rate, double hostFrom, double hostTo)
{
    if (context->def.eventCapacity == 0)
    {
        mtwnSweepEvents(context, work, player, to, rate, hostFrom, hostTo);
        return;
    }
    mtwnClipPlay* play = &mtwnPlayerAt(context, player)->play;
    mtwnSharing* sharing = &work->sharing;
    SweepKey key;
    int8_t way = 0;
    bool moves = KeyOf(play, to, rate, hostFrom, hostTo, &key, &way);
    bool same = moves && SameSweep(sharing, &key);
    if (same && sharing->sweepKept)
    {
        // One in step with the last noted makes its records, but for its id.
        play->swept = to;
        play->sweptWay = way;
        mtwnPlayerId id = mtwnPlayerIdAt(context, player);
        for (uint32_t r = 0; r < sharing->recordCount; ++r)
        {
            mtwnEvent event = sharing->records[r].event;
            event.player = id;
            mtwnWorkEvent(context, work, &event, sharing->records[r].along);
        }
        return;
    }
    if (!moves)
    {
        mtwnSweepEvents(context, work, player, to, rate, hostFrom, hostTo);
        return;
    }
    KeepSweep(sharing, &key);
    // The second in step keeps its records for the rest.
    sharing->capture = same && sharing->recordCapacity > 0;
    mtwnSweepEvents(context, work, player, to, rate, hostFrom, hostTo);
    sharing->sweepKept = sharing->capture;
    sharing->capture = false;
}
