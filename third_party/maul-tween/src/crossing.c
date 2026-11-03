// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Crossings. A level's path: its iterations from the
// first its rate reaches to the last, each from its start to its end;
// iteration j at progress q has the child time ChildAt(j, q). A local time
// reaches part of the path: none, all, or every iteration before k and
// iteration k up to q, q itself in or out. Where two positions share a
// local time (an iteration's end and the next one's start with no repeat
// delay between) both are reached together. The count below a local time
// is constants for the whole iterations reached plus the iteration
// reached partly, whose moving end is counted at the level below: one
// walk down the chain.

#include "crossing.h"

#include "fpenv.h"
#include "invariant.h"
#include "timing_internal.h"
#include "work.h"

#include "maul-tween/easing.h"

#include <math.h>
#include <string.h>

// What a local time reaches of a level's path.
typedef enum ReachKind
{
    s_none,
    s_all,
    s_part,
} ReachKind;

typedef struct Reach
{
    ReachKind kind;
    bool inclusive;
    double iteration;
    double progress;
} Reach;

// A path's shape: its iterations, the traversal's direction, and whether
// it is a single point or runs forever on one iteration.
typedef struct Shape
{
    double active;
    double first;
    double firstProgress;
    double last;
    double lastProgress;
    bool forwardRate;
    bool point;
    bool unbounded;
} Shape;

static Shape ShapeOf(const mtwnTiming* timing, double active)
{
    Shape shape = {0};
    double d = timing->duration;
    double n = timing->iterations;
    double s = timing->iterationStart;
    shape.active = active;
    shape.forwardRate = timing->playbackRate > 0.0;
    shape.point = d == 0.0 || n == 0.0;
    shape.unbounded = isinf(d) && !shape.point;
    shape.first = floor(s);
    shape.firstProgress = s - shape.first;
    if (isinf(n))
    {
        shape.last = HUGE_VAL;
        shape.lastProgress = 1.0;
    }
    else
    {
        shape.last = ceil(s + n) - 1.0;
        shape.lastProgress = s + n - shape.last;
    }
    return shape;
}

static double ActiveOf(const mtwnTiming* timing)
{
    mtwnTimingResult result;
    mtwnComputeTimingWork(timing, 0.0, false, &result);
    return result.activeDuration;
}

// A child time on an iteration running forwards or not, at a progress.
static double ChildAt(const mtwnTiming* timing, bool forwards, double q)
{
    double eased = 0.0;
    if (!forwards && timing->alternateEasing == mtwn_alternateRepeatsShape)
    {
        eased = 1.0 - mtwnSampleTimelineEasingWork(&timing->easing, q, false, NULL);
    }
    else
    {
        eased = mtwnSampleTimelineEasingWork(&timing->easing, forwards ? q : 1.0 - q, false, NULL);
    }
    return eased * timing->duration;
}

static bool Forwards(const mtwnTiming* timing, double iteration)
{
    return mtwnIterationForwards(timing->direction, iteration);
}

// The progress an iteration's traversal starts and ends at.
static double StartProgress(const Shape* shape, double j)
{
    if (shape->forwardRate)
    {
        return j == shape->first ? shape->firstProgress : 0.0;
    }
    return j == shape->last ? shape->lastProgress : 1.0;
}

static double EndProgress(const Shape* shape, double j)
{
    if (shape->forwardRate)
    {
        return j == shape->last ? shape->lastProgress : 1.0;
    }
    return j == shape->first ? shape->firstProgress : 0.0;
}

static Reach Part(double j, double q, bool inclusive)
{
    return (Reach){s_part, inclusive, j, q};
}

// The position a scaled time is at: its iteration and progress, and
// whether it lies in a repeat delay (at the iteration's end).
static Reach Position(const mtwnTiming* timing, const Shape* shape, double scaled, bool* inDelay)
{
    double d = timing->duration;
    double s = timing->iterationStart;
    double j = 0.0;
    double q = 0.0;
    *inDelay = false;
    if (!mtwnHasRepeatDelay(timing))
    {
        double overall = scaled / d + s;
        j = floor(overall);
        q = overall - j;
    }
    else
    {
        double cycle = d + timing->repeatDelay;
        double within = 0.0;
        j = mtwnCycleOf(scaled, s, d, cycle, &within);
        // As the timing model forms them, so a child time here is the one
        // Times and the values see, bit for bit: a time below 0 within j
        // is in the delay before j, and a progress rounded to the end has
        // reached the delay.
        if (within < 0.0)
        {
            j -= 1.0;
            within += cycle;
        }
        double overall = within < d ? j + within / d : j + 1.0;
        *inDelay = within >= d || overall - j >= 1.0;
        q = *inDelay ? 1.0 : overall - j;
        if (*inDelay && within <= d && shape->forwardRate)
        {
            // At the delay's start, going forwards: the end is reached
            // now.
            *inDelay = false;
        }
    }
    // Past the doubles, where only an endless node's iteration goes, the
    // timing model holds it at the start's fraction, 1 for none: the end of
    // its infinite iteration, where q would be infinity less infinity.
    if (isinf(j))
    {
        q = s > floor(s) ? s - floor(s) : 1.0;
        *inDelay = false;
    }
    // Rounding at the ends stays on the path.
    if (j < shape->first || (j == shape->first && q < shape->firstProgress))
    {
        j = shape->first;
        q = shape->firstProgress;
    }
    if (j > shape->last || (j == shape->last && q > shape->lastProgress))
    {
        j = shape->last;
        q = shape->lastProgress;
        *inDelay = false;
    }
    return Part(j, q, true);
}

// What a local time reaches at an iteration's start: going forwards the
// previous end shares the time without a delay; going backwards it is
// reached as the start is left, whatever lies between.
static Reach AtStart(const mtwnTiming* timing, const Shape* shape, double j, bool strict)
{
    if (shape->forwardRate)
    {
        if (!strict)
        {
            return Part(j, 0.0, true);
        }
        return mtwnHasRepeatDelay(timing) ? Part(j, 0.0, false) : Part(j - 1.0, 1.0, false);
    }
    return strict ? Part(j, 0.0, false) : Part(j - 1.0, 1.0, true);
}

// What a local time reaches of a level's path with finite iterations of
// positive duration; strict leaves out positions at the time itself.
static Reach Locate(const mtwnTiming* timing, const Shape* shape, double local, bool strict)
{
    double a = local - timing->delay;
    if (a < 0.0 || (a == 0.0 && strict))
    {
        return (Reach){s_none, false, 0.0, 0.0};
    }
    double startJ = shape->forwardRate ? shape->first : shape->last;
    if (a == 0.0)
    {
        return Part(startJ, StartProgress(shape, startJ), true);
    }
    if (a > shape->active)
    {
        return (Reach){s_all, true, 0.0, 0.0};
    }
    if (a == shape->active)
    {
        double endJ = shape->forwardRate ? shape->last : shape->first;
        return strict ? Part(endJ, EndProgress(shape, endJ), false)
                      : (Reach){s_all, true, 0.0, 0.0};
    }
    double rate = fabs(timing->playbackRate);
    double scaled = shape->forwardRate ? a * rate : (shape->active - a) * rate;
    bool inDelay = false;
    Reach at = Position(timing, shape, scaled, &inDelay);
    double j = at.iteration;
    if (inDelay)
    {
        // Going forwards the iteration's end was reached at the delay's
        // start; backwards the next start's time is its time too.
        return Part(j, 1.0, true);
    }
    if (at.progress == 0.0 && j > shape->first)
    {
        return AtStart(timing, shape, j, strict);
    }
    return Part(j, at.progress, !strict);
}

// Adds a pair, swapped or not.
static void Add(mtwnCrossings* sum, bool swap, mtwnCrossings pair)
{
    sum->same += swap ? pair.opposite : pair.same;
    sum->opposite += swap ? pair.same : pair.opposite;
}

// A pair times a count of iterations; none for none either way, so a
// constant left unwalked (0) or endless (a count may be infinite) never
// meets 0 * c. Kept out of line, as Less is: each is called in several
// places.
static double Times(double x, double times)
{
    return x == 0.0 || times == 0.0 ? 0.0 : x * times;
}

MTWN_NOINLINE static mtwnCrossings Scaled(mtwnCrossings pair, double times)
{
    return (mtwnCrossings){Times(pair.same, times), Times(pair.opposite, times)};
}

// What a later reach adds to an earlier one, each way. Where both are
// infinite, the node is past the doubles at both times, held at the end
// of its infinite iteration as the timing model holds it: nothing lies
// between, where the difference would be infinity less infinity.
MTWN_NOINLINE static mtwnCrossings Less(mtwnCrossings later, mtwnCrossings earlier)
{
    double same = later.same == earlier.same ? 0.0 : later.same - earlier.same;
    double opposite = later.opposite == earlier.opposite ? 0.0 : later.opposite - earlier.opposite;
    return (mtwnCrossings){same, opposite};
}

// The iterations strictly between two that run forwards, and those that
// run backwards. Up to an endless node's last, past the doubles, there are
// infinitely many of each parity, where all less even would be infinity
// less infinity.
static void Between(const mtwnTiming* timing, double a, double b, double* forwardsOut,
                    double* backwardsOut)
{
    double lo = (a < b ? a : b) + 1.0;
    double hi = (a < b ? b : a) - 1.0;
    double all = hi >= lo ? hi - lo + 1.0 : 0.0;
    double even = hi >= lo ? floor(hi / 2.0) - ceil(lo / 2.0) + 1.0 : 0.0;
    double odd = isinf(all) ? all : all - even;
    switch (timing->direction)
    {
    case mtwn_directionNormal:
        *forwardsOut = all;
        *backwardsOut = 0.0;
        break;
    case mtwn_directionReverse:
        *forwardsOut = 0.0;
        *backwardsOut = all;
        break;
    case mtwn_directionAlternate:
        *forwardsOut = even;
        *backwardsOut = odd;
        break;
    default:
        *forwardsOut = odd;
        *backwardsOut = even;
        break;
    }
}

// The iteration starts of a node a local time has reached.
static double IterationsReached(const mtwnLeaf* leaf, double local, bool strict)
{
    const mtwnTiming* timing = &leaf->timing;
    Shape shape = ShapeOf(timing, leaf->activeKnown ? leaf->activeDuration : ActiveOf(timing));
    if (shape.point || shape.unbounded)
    {
        return 0.0;
    }
    Reach reach = Locate(timing, &shape, local, strict);
    if (reach.kind == s_none)
    {
        return 0.0;
    }
    if (reach.kind == s_all)
    {
        return shape.last - shape.first;
    }
    double k = reach.iteration;
    bool start = reach.progress == 0.0 && k > shape.first;
    if (shape.forwardRate)
    {
        return k > shape.first ? k - shape.first - (start && !reach.inclusive ? 1.0 : 0.0) : 0.0;
    }
    // Backwards a start reached is always followed by the end before it.
    return shape.last - k;
}

static mtwnCrossings LeafBelow(const mtwnLeaf* leaf, double y, bool strict)
{
    if (leaf->kind == mtwn_leafIterations)
    {
        return (mtwnCrossings){IterationsReached(leaf, y - leaf->at, strict), 0.0};
    }
    bool reached = strict ? leaf->at < y : leaf->at <= y;
    return (mtwnCrossings){reached ? 1.0 : 0.0, 0.0};
}

static double NextStart(const mtwnLevel* levels, uint32_t count, uint32_t i)
{
    return i + 1 < count ? levels[i + 1].start : 0.0;
}

// The whole reach of a prepared level, relative to its local time.
static mtwnCrossings All(const mtwnLevel* level, const Shape* shape)
{
    mtwnCrossings sum = level->cover[0];
    double firstJ = shape->forwardRate ? shape->first : shape->last;
    double lastJ = shape->forwardRate ? shape->last : shape->first;
    if (firstJ != lastJ)
    {
        double forwards = 0.0;
        double backwards = 0.0;
        Between(&level->timing, firstJ, lastJ, &forwards, &backwards);
        Add(&sum, false, Scaled(level->cover[1], forwards));
        Add(&sum, false, Scaled(level->cover[2], backwards));
        Add(&sum, false, level->cover[3]);
    }
    return sum;
}

// The whole iterations a reach of iteration k covers before k.
static mtwnCrossings Prefix(const mtwnLevel* level, const Shape* shape, double k)
{
    mtwnCrossings sum = {0.0, 0.0};
    double firstJ = shape->forwardRate ? shape->first : shape->last;
    if (k == firstJ)
    {
        return sum;
    }
    sum = level->cover[0];
    double forwards = 0.0;
    double backwards = 0.0;
    Between(&level->timing, firstJ, k, &forwards, &backwards);
    Add(&sum, false, Scaled(level->cover[1], forwards));
    Add(&sum, false, Scaled(level->cover[2], backwards));
    return sum;
}

// Where a level's reach leaves its moving part to the level below: the
// iteration reached partly, the child times it runs between and ends at,
// whether it runs down the local time, whether its end is in, and the
// constants kept for its start and its end.
typedef struct Partial
{
    double iteration;
    double from;
    double to;
    double end;
    bool down;
    bool inclusive;
    uint32_t start;
    uint32_t finish;
} Partial;

// A prepared level's shape, as its preparation kept it.
static Shape LevelShape(const mtwnLevel* level)
{
    Shape shape = {level->activeDuration, level->firstIteration, level->firstProgress,
                   level->lastIteration,  level->lastProgress,   level->forwardRate,
                   level->point,          level->unbounded};
    return shape;
}

// The level's own count at a local time into sum: whole, or the whole
// iterations before the one reached partly, which partial then holds.
static bool Reaching(const mtwnLevel* level, double x, bool strict, mtwnCrossings* sum,
                     Partial* partial)
{
    const mtwnTiming* timing = &level->timing;
    Shape shape = LevelShape(level);
    double a = x - timing->delay;
    if (a < 0.0 || (a == 0.0 && strict))
    {
        return false;
    }
    if (shape.point)
    {
        *sum = level->pointBelow;
        return false;
    }
    if (shape.unbounded)
    {
        // One forward iteration from 0, at the rate; reversed, at infinity.
        *partial =
            (Partial){shape.first, 0.0, a * timing->playbackRate, HUGE_VAL, false, !strict, 1, 0};
        return Forwards(timing, shape.first);
    }
    Reach reach = Locate(timing, &shape, x, strict);
    if (reach.kind == s_all)
    {
        *sum = All(level, &shape);
        return false;
    }
    double k = reach.iteration;
    *sum = Prefix(level, &shape, k);
    bool forwards = Forwards(timing, k);
    partial->iteration = k;
    partial->from = ChildAt(timing, forwards, StartProgress(&shape, k));
    partial->to = ChildAt(timing, forwards, reach.progress);
    partial->end = ChildAt(timing, forwards, EndProgress(&shape, k));
    partial->down = shape.forwardRate != forwards;
    partial->inclusive = reach.inclusive;
    double firstJ = shape.forwardRate ? shape.first : shape.last;
    double lastJ = shape.forwardRate ? shape.last : shape.first;
    partial->start = k == firstJ ? 0u : (forwards ? 1u : 2u);
    partial->finish = k == lastJ ? 2u : (forwards ? 0u : 1u);
    return true;
}

static mtwnCrossings Below(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                           uint32_t i, double x, bool strict);
static mtwnCrossings Gained(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                            uint32_t i, double xa, bool sa, double xb, bool sb, bool split);

// What the level below reaches of a partly reached iteration, from its
// start to the reach's child time, swapped where the iteration runs down:
// counted between the two (walk), or as the reach there less the constant
// kept for the start. It, ToEnd, BelowAt and the count are kept out of
// line, each called from several places.
MTWN_NOINLINE static mtwnCrossings FromStart(const mtwnLevel* levels, uint32_t count,
                                             const mtwnLeaf* leaf, uint32_t i,
                                             const Partial* partial, bool walk, bool split)
{
    double next = NextStart(levels, count, i);
    double at = partial->to - next;
    double from = partial->from - next;
    const mtwnCrossings* start = levels[i].startBelow[partial->start];
    mtwnCrossings part = {0.0, 0.0};
    if (partial->from <= partial->to)
    {
        // Points in [from, to] or [from, to).
        part = walk ? Gained(levels, count, leaf, i + 1, from, true, at, !partial->inclusive, split)
                    : Less(Below(levels, count, leaf, i + 1, at, !partial->inclusive), start[1]);
    }
    else
    {
        // Points in [to, from] or (to, from].
        part = walk ? Gained(levels, count, leaf, i + 1, at, partial->inclusive, from, false, split)
                    : Less(start[0], Below(levels, count, leaf, i + 1, at, partial->inclusive));
    }
    mtwnCrossings sum = {0.0, 0.0};
    Add(&sum, partial->down, part);
    return sum;
}

// What the level below reaches of a partly reached iteration beyond the
// reach, to the iteration's end, as FromStart: with it, the iteration's
// coverage.
MTWN_NOINLINE static mtwnCrossings ToEnd(const mtwnLevel* levels, uint32_t count,
                                         const mtwnLeaf* leaf, uint32_t i, const Partial* partial,
                                         bool walk, bool split)
{
    double next = NextStart(levels, count, i);
    double at = partial->to - next;
    double to = partial->end - next;
    const mtwnCrossings* end = levels[i].endBelow[partial->finish];
    mtwnCrossings part = {0.0, 0.0};
    // Which way the iteration runs: at its start the reach's child time is
    // its start's, whichever way.
    if (partial->from <= partial->end)
    {
        // Points in (to, end] or [to, end].
        part = walk ? Gained(levels, count, leaf, i + 1, at, !partial->inclusive, to, false, split)
                    : Less(end[0], Below(levels, count, leaf, i + 1, at, !partial->inclusive));
    }
    else
    {
        // Points in [end, to) or [end, to].
        part = walk ? Gained(levels, count, leaf, i + 1, to, true, at, partial->inclusive, split)
                    : Less(Below(levels, count, leaf, i + 1, at, partial->inclusive), end[1]);
    }
    mtwnCrossings sum = {0.0, 0.0};
    Add(&sum, partial->down, part);
    return sum;
}

// What a local time of level i (or the leaf, at the bottom) reaches: one
// walk down the chain, at most MTWN_MAX_CLIP_DEPTH + 2 levels deep.
static mtwnCrossings Below(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                           uint32_t i, double x, bool strict)
{
    if (i == count)
    {
        return LeafBelow(leaf, x, strict);
    }
    mtwnCrossings sum = {0.0, 0.0};
    Partial partial;
    if (Reaching(&levels[i], x, strict, &sum, &partial))
    {
        Add(&sum, false, FromStart(levels, count, leaf, i, &partial, false, false));
    }
    return sum;
}

// The coverage of the whole iterations strictly between two.
static mtwnCrossings Among(const mtwnLevel* level, double a, double b)
{
    double forwards = 0.0;
    double backwards = 0.0;
    Between(&level->timing, a, b, &forwards, &backwards);
    mtwnCrossings sum = Scaled(level->cover[1], forwards);
    Add(&sum, false, Scaled(level->cover[2], backwards));
    return sum;
}

// The crossings at points a local time xb of level i (or the leaf, at the
// bottom) reaches beyond those an earlier xa reaches, each strictly or not:
// a walk down the chain with both, at most MTWN_MAX_CLIP_DEPTH + 2 levels
// deep. Where both lie in one iteration, what came before it is the same
// and the level below counts between their child times. Where they part
// in two iterations, a's from a on, the whole ones between and b's up to
// b: each end's part is walked on below with its iteration's start or
// end. What either end reaches from the level's start is not added up, so
// a difference far smaller than that stays exact; where both reaches are
// past the doubles (Less), the node is held and nothing lies between. A
// walk that split so once (split) takes a second split's parts as the
// reaches there less the constants kept for the iterations' starts and
// ends, so a count costs two walks and four reaches at most, not a walk
// that doubles at every level.
static mtwnCrossings Gained(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                            uint32_t i, double xa, bool sa, double xb, bool sb, bool split)
{
    if (i == count)
    {
        return Less(LeafBelow(leaf, xb, sb), LeafBelow(leaf, xa, sa));
    }
    const mtwnLevel* level = &levels[i];
    mtwnCrossings a = {0.0, 0.0};
    mtwnCrossings b = {0.0, 0.0};
    Partial pa;
    Partial pb;
    bool partA = Reaching(level, xa, sa, &a, &pa);
    bool partB = Reaching(level, xb, sb, &b, &pb);
    if (partB && !partA)
    {
        // Nothing reached at a.
        Add(&b, false, FromStart(levels, count, leaf, i, &pb, true, split));
        return b;
    }
    if (!partA || (!partB && !isfinite(pa.iteration)))
    {
        // Each whole, none or a point's; or a held past the doubles.
        if (partA)
        {
            Add(&a, false, FromStart(levels, count, leaf, i, &pa, true, split));
        }
        return Less(b, a);
    }
    bool up = pa.from <= pa.to;
    if (partB && pa.iteration == pb.iteration && up == (pb.from <= pb.to))
    {
        double next = NextStart(levels, count, i);
        mtwnCrossings part = up ? Gained(levels, count, leaf, i + 1, pa.to - next, !pa.inclusive,
                                         pb.to - next, !pb.inclusive, split)
                                : Gained(levels, count, leaf, i + 1, pb.to - next, pb.inclusive,
                                         pa.to - next, pa.inclusive, split);
        mtwnCrossings sum = {0.0, 0.0};
        Add(&sum, pa.down, part);
        return sum;
    }
    if (partB && pa.iteration == pb.iteration)
    {
        return Less(FromStart(levels, count, leaf, i, &pb, !split, true),
                    FromStart(levels, count, leaf, i, &pa, !split, true));
    }
    Shape shape = LevelShape(level);
    double lastJ = shape.forwardRate ? shape.last : shape.first;
    if (!partB)
    {
        // b whole: a's iteration from a on, and every one after it.
        mtwnCrossings sum = ToEnd(levels, count, leaf, i, &pa, true, split);
        if (pa.iteration != lastJ)
        {
            Add(&sum, false, Among(level, pa.iteration, lastJ));
            Add(&sum, false, level->cover[3]);
        }
        return sum;
    }
    mtwnCrossings sum = ToEnd(levels, count, leaf, i, &pa, !split, true);
    Add(&sum, false, Among(level, pa.iteration, pb.iteration));
    Add(&sum, false, FromStart(levels, count, leaf, i, &pb, !split, true));
    return sum;
}

// The points of one traversal of an iteration, from a progress to
// another, relative to the level's local time.
static mtwnCrossings Cover(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                           uint32_t i, bool forwards, double from, double to)
{
    const mtwnLevel* level = &levels[i];
    double next = NextStart(levels, count, i);
    double childFrom = ChildAt(&level->timing, forwards, from);
    double childTo = ChildAt(&level->timing, forwards, to);
    double lo = childFrom < childTo ? childFrom : childTo;
    double hi = childFrom < childTo ? childTo : childFrom;
    mtwnCrossings later = Below(levels, count, leaf, i + 1, hi - next, false);
    mtwnCrossings sum = Less(later, Below(levels, count, leaf, i + 1, lo - next, true));
    bool down = (level->timing.playbackRate > 0.0 ? 1 : -1) * (forwards ? 1 : -1) < 0;
    return down ? (mtwnCrossings){sum.opposite, sum.same} : sum;
}

static bool SameBits(double a, double b)
{
    uint64_t x = 0;
    uint64_t y = 0;
    memcpy(&x, &a, sizeof x);
    memcpy(&y, &b, sizeof y);
    return x == y;
}

// Below at child times, closed and strict, each worked out once: one of
// the same child time as one before is the same bits, so it is copied.
MTWN_NOINLINE static void BelowAt(mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                                  uint32_t i, const double* times, const bool* runs,
                                  mtwnCrossings (*out)[2])
{
    double next = NextStart(levels, count, i);
    for (int k = 0; k < 3; ++k)
    {
        int same = -1;
        for (int m = 0; m < k; ++m)
        {
            same = runs[m] && SameBits(times[m], times[k]) ? m : same;
        }
        for (int strict = 0; strict < 2 && runs[k]; ++strict)
        {
            out[k][strict] = same >= 0
                                 ? out[same][strict]
                                 : Below(levels, count, leaf, i + 1, times[k] - next, strict != 0);
        }
    }
}

// The constants of a level of iterations of positive, finite duration:
// below at its iteration starts and ends, and its iterations' coverage.
static void PrepareIterations(mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf, uint32_t i,
                              const Shape* shape)
{
    mtwnLevel* level = &levels[i];
    const mtwnTiming* timing = &level->timing;
    double firstJ = shape->forwardRate ? shape->first : shape->last;
    double lastJ = shape->forwardRate ? shape->last : shape->first;
    // A whole iteration the direction never runs one way is never read
    // that way (no start or end of it is reached, and Between counts none
    // of it), so its counts stay 0. A count of the same inputs as one
    // worked out is the same bits: it is copied.
    bool runs[3] = {true, level->timing.direction != mtwn_directionReverse,
                    level->timing.direction != mtwn_directionNormal};
    bool firstForwards = Forwards(timing, firstJ);
    double firstFrom = StartProgress(shape, firstJ);
    double firstTo = EndProgress(shape, firstJ);
    double full0 = shape->forwardRate ? 0.0 : 1.0;
    double starts[3] = {ChildAt(timing, firstForwards, firstFrom), ChildAt(timing, true, full0),
                        ChildAt(timing, false, full0)};
    BelowAt(levels, count, leaf, i, starts, runs, level->startBelow);
    // The ends of full forward and reversed iterations, and the last's.
    bool ends[3] = {runs[1], runs[2], isfinite(lastJ)};
    double finishes[3] = {
        ChildAt(timing, true, 1.0 - full0), ChildAt(timing, false, 1.0 - full0),
        ends[2] ? ChildAt(timing, Forwards(timing, lastJ), EndProgress(shape, lastJ)) : 0.0};
    BelowAt(levels, count, leaf, i, finishes, ends, level->endBelow);
    level->cover[0] = Cover(levels, count, leaf, i, firstForwards, firstFrom, firstTo);
    for (int way = 1; way < 3; ++way)
    {
        bool forwards = way == 1;
        bool asFirst = forwards == firstForwards && SameBits(firstFrom, full0) &&
                       SameBits(firstTo, 1.0 - full0);
        if (runs[way])
        {
            level->cover[way] = asFirst
                                    ? level->cover[0]
                                    : Cover(levels, count, leaf, i, forwards, full0, 1.0 - full0);
        }
    }
    if (isfinite(lastJ))
    {
        level->cover[3] = Cover(levels, count, leaf, i, Forwards(timing, lastJ),
                                StartProgress(shape, lastJ), EndProgress(shape, lastJ));
    }
}

static void PrepareLevel(mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf, uint32_t i)
{
    mtwnLevel* level = &levels[i];
    const mtwnTiming* timing = &level->timing;
    if (!level->activeKnown)
    {
        level->activeDuration = ActiveOf(timing);
    }
    Shape shape = ShapeOf(timing, level->activeDuration);
    level->firstIteration = shape.first;
    level->firstProgress = shape.firstProgress;
    level->lastIteration = shape.last;
    level->lastProgress = shape.lastProgress;
    level->forwardRate = shape.forwardRate;
    level->point = shape.point;
    level->unbounded = shape.unbounded;
    // Every count below a count can read is written, those this shape
    // leaves unread as 0, so the level needs no clearing first.
    memset(level->cover, 0, sizeof level->cover);
    memset(level->startBelow, 0, sizeof level->startBelow);
    memset(level->endBelow, 0, sizeof level->endBelow);
    level->pointBelow = (mtwnCrossings){0.0, 0.0};
    double next = NextStart(levels, count, i);
    if (shape.point)
    {
        // An endless one of no iterations gives its children what the
        // values do: its active time, 0, going forwards, infinity reversed.
        bool forwards = Forwards(timing, shape.first);
        double at = timing->duration == 0.0   ? 0.0
                    : isinf(timing->duration) ? (forwards ? 0.0 : HUGE_VAL)
                                              : ChildAt(timing, forwards, shape.firstProgress);
        mtwnCrossings later = Below(levels, count, leaf, i + 1, at - next, false);
        level->pointBelow = Less(later, Below(levels, count, leaf, i + 1, at - next, true));
        return;
    }
    if (shape.unbounded)
    {
        for (int strict = 0; strict < 2; ++strict)
        {
            level->startBelow[1][strict] = Below(levels, count, leaf, i + 1, -next, strict != 0);
        }
        return;
    }
    PrepareIterations(levels, count, leaf, i, &shape);
}

uint32_t mtwnPassedLevels(const mtwnLevel* levels, uint32_t count)
{
    const mtwnLevel* top = &levels[0];
    const mtwnCrossings none = top->startBelow[1][1];
    bool passes = count >= 1 && top->unbounded && top->start == 0.0 && top->timing.delay == 0.0 &&
                  top->timing.playbackRate == 1.0 && Forwards(&top->timing, top->firstIteration);
    return passes && none.same == 0.0 && none.opposite == 0.0 ? 1u : 0u;
}

void mtwnPrepareLevels(mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf)
{
    for (uint32_t i = count; i > 0; --i)
    {
        PrepareLevel(levels, count, leaf, i - 1);
    }
}

MTWN_NOINLINE mtwnCrossings mtwnCountCrossings(const mtwnLevel* levels, uint32_t count,
                                               const mtwnLeaf* leaf, double from, double to,
                                               bool fromClosed)
{
    double start = count > 0 ? levels[0].start : 0.0;
    mtwnCrossings sum = {0.0, 0.0};
    if (to >= from)
    {
        if (to == from && !fromClosed)
        {
            return sum;
        }
        sum = Gained(levels, count, leaf, 0, from - start, fromClosed, to - start, false, false);
        // What is reached by a later time includes what is reached by an
        // earlier one, so no count is negative.
        MTWN_ASSERT(!(sum.same < 0.0) && !(sum.opposite < 0.0));
        return sum;
    }
    sum = Gained(levels, count, leaf, 0, to - start, true, from - start, !fromClosed, false);
    MTWN_ASSERT(!(sum.same < 0.0) && !(sum.opposite < 0.0));
    // The sweep runs against the coordinate: the leaf moves the other way.
    return (mtwnCrossings){sum.opposite, sum.same};
}

static double Of(mtwnCrossings c, mtwnCrossingWay way)
{
    return way == mtwn_crossingForwards    ? c.same
           : way == mtwn_crossingBackwards ? c.opposite
                                           : c.same + c.opposite;
}

// The doubles in their order as integers: the bisection below halves the
// doubles between two times, so it ends exactly, after at most 64 halvings.
static int64_t Ordered(double x)
{
    int64_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    return bits < 0 ? INT64_MIN - bits : bits;
}

static double Unordered(int64_t ordered)
{
    int64_t bits = ordered < 0 ? INT64_MIN - ordered : ordered;
    double x = 0.0;
    memcpy(&x, &bits, sizeof x);
    return x;
}

// A search: the sweep, the way, which end it is near, and the bracket
// of ordered doubles it halves. The count does not reach one at near and
// does at far.
typedef struct Search
{
    const mtwnLevel* levels;
    uint32_t count;
    const mtwnLeaf* leaf;
    double from;
    double to;
    bool fromClosed;
    bool last;
    bool up;
    mtwnCrossingWay way;
    int64_t near;
    int64_t far;
} Search;

// Whether the sweep part a search tests counts a crossing: from the sweep's
// start to the ordered double t, or from t to its end.
static bool Found(const Search* search, int64_t t)
{
    double at = Unordered(t);
    mtwnCrossings c = search->last ? mtwnCountCrossings(search->levels, search->count, search->leaf,
                                                        at, search->to, true)
                                   : mtwnCountCrossings(search->levels, search->count, search->leaf,
                                                        search->from, at, search->fromClosed);
    return Of(c, search->way) >= 1.0;
}

// Moves the bracket's end the ordered double t tests: far when it counts,
// near when not.
static void Test(Search* search, int64_t t)
{
    if (Found(search, t))
    {
        search->far = t;
    }
    else
    {
        search->near = t;
    }
}

// Whether ordered b lies strictly beyond a, going up or down.
static bool Beyond(bool up, int64_t a, int64_t b)
{
    return up ? b > a : b < a;
}

// A guess past the near end is tested, and when it counts so is its
// neighbor toward the near end: a right guess leaves the two neighbors
// the halving ends with. A guess past the far end is the far end, which
// counts: an interpolated guess can pass the sweep's end by a double.
static void TestGuess(Search* search, double guess)
{
    int64_t g = isnan(guess) ? search->near : Ordered(guess);
    if (!Beyond(search->up, search->near, g))
    {
        return;
    }
    if (Beyond(search->up, search->far, g))
    {
        g = search->far;
    }
    else
    {
        Test(search, g);
    }
    int64_t before = search->up ? g - 1 : g + 1;
    if (search->far == g && Beyond(search->up, search->near, before))
    {
        Test(search, before);
    }
}

double mtwnFindCrossingFrom(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                            double from, double to, bool fromClosed, bool last, mtwnCrossingWay way,
                            double guess)
{
    // The point at the near end itself.
    double end = last ? to : from;
    if ((last || fromClosed) &&
        Of(mtwnCountCrossings(levels, count, leaf, end, end, true), way) >= 1.0)
    {
        return end;
    }
    // The count is not reached at the near end and is at the far one;
    // halve the doubles between until they are neighbors.
    Search search = {levels,
                     count,
                     leaf,
                     from,
                     to,
                     fromClosed,
                     last,
                     false,
                     way,
                     Ordered(last ? to : from),
                     Ordered(last ? from : to)};
    search.up = search.far > search.near;
    TestGuess(&search, guess);
    for (;;)
    {
        uint64_t distance = search.up ? (uint64_t)search.far - (uint64_t)search.near
                                      : (uint64_t)search.near - (uint64_t)search.far;
        if (distance <= 1)
        {
            return Unordered(search.far);
        }
        int64_t half = (int64_t)(distance / 2);
        Test(&search, search.up ? search.near + half : search.near - half);
    }
}

double mtwnIterationAt(const mtwnLevel* levels, uint32_t count, double t)
{
    double x = t - levels[0].start;
    double iteration = 0.0;
    for (uint32_t i = 0; i < count; ++i)
    {
        mtwnTiming timing = levels[i].timing;
        timing.fill = mtwn_fillBoth;
        mtwnTimingResult result;
        mtwnComputeTimelineTimingWork(&timing, x, false, &result);
        iteration = result.iteration;
        double child = 0.0;
        if (isinf(timing.duration))
        {
            child = result.forwards ? result.activeTime * timing.playbackRate : HUGE_VAL;
        }
        else
        {
            child = result.transformedProgress * timing.duration;
        }
        x = child - NextStart(levels, count, i);
    }
    return iteration;
}
