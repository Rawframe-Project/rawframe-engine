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
        eased = 1.0 - mtwnSampleEasingWork(&timing->easing, q, false);
    }
    else
    {
        eased = mtwnSampleEasingWork(&timing->easing, forwards ? q : 1.0 - q, false);
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

// Adds a pair, swapped or not, times a sign.
static void Add(mtwnCrossings* sum, double sign, bool swap, mtwnCrossings pair)
{
    sum->same += sign * (swap ? pair.opposite : pair.same);
    sum->opposite += sign * (swap ? pair.same : pair.opposite);
}

// A pair times a count of iterations; none for none, so a constant left
// unwalked (0) or endless (a count may be infinite) never meets 0 * c.
static mtwnCrossings Scaled(mtwnCrossings pair, double times)
{
    if (times == 0.0)
    {
        return (mtwnCrossings){0.0, 0.0};
    }
    return (mtwnCrossings){pair.same * times, pair.opposite * times};
}

// The iterations strictly between two that run forwards, and all of them.
static void Between(const mtwnTiming* timing, double a, double b, double* forwardsOut,
                    double* allOut)
{
    double lo = (a < b ? a : b) + 1.0;
    double hi = (a < b ? b : a) - 1.0;
    double all = hi >= lo ? hi - lo + 1.0 : 0.0;
    double even = hi >= lo ? floor(hi / 2.0) - ceil(lo / 2.0) + 1.0 : 0.0;
    *allOut = all;
    switch (timing->direction)
    {
    case mtwn_directionNormal:
        *forwardsOut = all;
        break;
    case mtwn_directionReverse:
        *forwardsOut = 0.0;
        break;
    case mtwn_directionAlternate:
        *forwardsOut = even;
        break;
    default:
        *forwardsOut = all - even;
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
        double all = 0.0;
        Between(&level->timing, firstJ, lastJ, &forwards, &all);
        Add(&sum, 1.0, false, Scaled(level->cover[1], forwards));
        Add(&sum, 1.0, false, Scaled(level->cover[2], all - forwards));
        Add(&sum, 1.0, false, level->cover[3]);
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
    double all = 0.0;
    Between(&level->timing, firstJ, k, &forwards, &all);
    Add(&sum, 1.0, false, Scaled(level->cover[1], forwards));
    Add(&sum, 1.0, false, Scaled(level->cover[2], all - forwards));
    return sum;
}

// A walk down the chain: the sum so far, the sign and swap the level
// below's count enters it with, and the point and strictness counted
// there.
typedef struct Walk
{
    mtwnCrossings sum;
    double sign;
    bool swap;
    bool strict;
    double x;
} Walk;

// Where a level's reach leaves its moving part to the level below: the
// child times its partly reached iteration runs between, whether it runs
// down the local time, the constant kept for its start, and whether its
// end is in. False when the level ends the walk.
typedef struct Partial
{
    double from;
    double to;
    bool down;
    bool inclusive;
    uint32_t start;
} Partial;

// A prepared level's shape, as its preparation kept it.
static Shape LevelShape(const mtwnLevel* level)
{
    Shape shape = {level->activeDuration, level->firstIteration, level->firstProgress,
                   level->lastIteration,  level->lastProgress,   level->forwardRate,
                   level->point,          level->unbounded};
    return shape;
}

static bool Reaching(const mtwnLevel* level, Walk* walk, Partial* partial)
{
    const mtwnTiming* timing = &level->timing;
    Shape shape = LevelShape(level);
    double a = walk->x - timing->delay;
    if (a < 0.0 || (a == 0.0 && walk->strict))
    {
        return false;
    }
    if (shape.point)
    {
        Add(&walk->sum, walk->sign, walk->swap, level->pointBelow);
        return false;
    }
    if (shape.unbounded)
    {
        // One forward iteration from 0, at the rate; reversed, at infinity.
        *partial = (Partial){0.0, a * timing->playbackRate, false, !walk->strict, 1};
        return Forwards(timing, shape.first);
    }
    Reach reach = Locate(timing, &shape, walk->x, walk->strict);
    if (reach.kind == s_all)
    {
        Add(&walk->sum, walk->sign, walk->swap, All(level, &shape));
        return false;
    }
    double k = reach.iteration;
    Add(&walk->sum, walk->sign, walk->swap, Prefix(level, &shape, k));
    bool forwards = Forwards(timing, k);
    double firstJ = shape.forwardRate ? shape.first : shape.last;
    partial->start = k == firstJ ? 0u : (forwards ? 1u : 2u);
    partial->from = ChildAt(timing, forwards, StartProgress(&shape, k));
    partial->to = ChildAt(timing, forwards, reach.progress);
    partial->down = shape.forwardRate != forwards;
    partial->inclusive = reach.inclusive;
    return true;
}

// The crossings at points reached by a local time of level i (or the
// leaf, at the bottom): one walk down the chain.
static mtwnCrossings Below(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                           uint32_t i, double x, bool strict)
{
    Walk walk = {{0.0, 0.0}, 1.0, false, strict, x};
    for (; i < count; ++i)
    {
        const mtwnLevel* level = &levels[i];
        Partial partial;
        if (!Reaching(level, &walk, &partial))
        {
            return walk.sum;
        }
        bool childSwap = walk.swap != partial.down;
        if (partial.from <= partial.to)
        {
            // Points in [from, to] or [from, to).
            Add(&walk.sum, -walk.sign, childSwap, level->startBelow[partial.start][1]);
            walk.strict = !partial.inclusive;
        }
        else
        {
            // Points in [to, from] or (to, from].
            Add(&walk.sum, walk.sign, childSwap, level->startBelow[partial.start][0]);
            walk.sign = -walk.sign;
            walk.strict = partial.inclusive;
        }
        walk.swap = childSwap;
        walk.x = partial.to - NextStart(levels, count, i);
    }
    Add(&walk.sum, walk.sign, walk.swap, LeafBelow(leaf, walk.x, walk.strict));
    return walk.sum;
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
    mtwnCrossings sum = Below(levels, count, leaf, i + 1, hi - next, false);
    Add(&sum, -1.0, false, Below(levels, count, leaf, i + 1, lo - next, true));
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

// The constants of a level of iterations of positive, finite duration:
// below at its iteration starts, and its iterations' coverage.
static void PrepareIterations(mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf, uint32_t i,
                              const Shape* shape)
{
    mtwnLevel* level = &levels[i];
    const mtwnTiming* timing = &level->timing;
    double next = NextStart(levels, count, i);
    double firstJ = shape->forwardRate ? shape->first : shape->last;
    double lastJ = shape->forwardRate ? shape->last : shape->first;
    // A whole iteration the direction never runs one way is never read
    // that way (no start of it is reached, and Between counts none of it),
    // so its counts stay 0. A count of the same inputs as one worked out
    // is the same bits: it is copied.
    bool runs[3] = {true, level->timing.direction != mtwn_directionReverse,
                    level->timing.direction != mtwn_directionNormal};
    bool firstForwards = Forwards(timing, firstJ);
    double firstFrom = StartProgress(shape, firstJ);
    double firstTo = EndProgress(shape, firstJ);
    double full0 = shape->forwardRate ? 0.0 : 1.0;
    double starts[3] = {ChildAt(timing, firstForwards, firstFrom), ChildAt(timing, true, full0),
                        ChildAt(timing, false, full0)};
    for (int k = 0; k < 3; ++k)
    {
        int same = -1;
        for (int m = 0; m < k; ++m)
        {
            same = runs[m] && SameBits(starts[m], starts[k]) ? m : same;
        }
        for (int strict = 0; strict < 2 && runs[k]; ++strict)
        {
            level->startBelow[k][strict] =
                same >= 0 ? level->startBelow[same][strict]
                          : Below(levels, count, leaf, i + 1, starts[k] - next, strict != 0);
        }
    }
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
        level->pointBelow = Below(levels, count, leaf, i + 1, at - next, false);
        Add(&level->pointBelow, -1.0, false, Below(levels, count, leaf, i + 1, at - next, true));
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

static mtwnCrossings Reached(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                             double t, bool strict, mtwnCrossingMemo* memo)
{
    if (memo != NULL)
    {
        for (uint32_t k = 0; k < memo->count; ++k)
        {
            if (memo->strict[k] == strict && SameBits(memo->at[k], t))
            {
                return memo->reached[k];
            }
        }
    }
    double local = count > 0 ? t - levels[0].start : t;
    mtwnCrossings reached = Below(levels, count, leaf, 0, local, strict);
    if (memo != NULL)
    {
        uint32_t k = memo->count < 8 ? memo->count++ : memo->next;
        memo->next = (k + 1) % 8;
        memo->at[k] = t;
        memo->strict[k] = strict;
        memo->reached[k] = reached;
    }
    return reached;
}

mtwnCrossings mtwnCountCrossingsMemo(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                                     double from, double to, bool fromClosed,
                                     mtwnCrossingMemo* memo)
{
    mtwnCrossings sum = {0.0, 0.0};
    if (to >= from)
    {
        sum = Reached(levels, count, leaf, to, false, memo);
        Add(&sum, -1.0, false, Reached(levels, count, leaf, from, fromClosed, memo));
        if (to == from && !fromClosed)
        {
            return (mtwnCrossings){0.0, 0.0};
        }
        // What is reached by a later time includes what is reached by an
        // earlier one, so no count is negative.
        MTWN_ASSERT(!(sum.same < 0.0) && !(sum.opposite < 0.0));
        return sum;
    }
    sum = Reached(levels, count, leaf, from, !fromClosed, memo);
    Add(&sum, -1.0, false, Reached(levels, count, leaf, to, true, memo));
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
    mtwnCrossingMemo* memo;
} Search;

// Whether the sweep part a search tests counts a crossing: from the sweep's
// start to the ordered double t, or from t to its end.
static bool Found(const Search* search, int64_t t)
{
    double at = Unordered(t);
    mtwnCrossings c =
        search->last ? mtwnCountCrossingsMemo(search->levels, search->count, search->leaf, at,
                                              search->to, true, search->memo)
                     : mtwnCountCrossingsMemo(search->levels, search->count, search->leaf,
                                              search->from, at, search->fromClosed, search->memo);
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
                            double guess, mtwnCrossingMemo* memo)
{
    // The point at the near end itself.
    double end = last ? to : from;
    if ((last || fromClosed) &&
        Of(mtwnCountCrossingsMemo(levels, count, leaf, end, end, true, memo), way) >= 1.0)
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
                     Ordered(last ? from : to),
                     memo};
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
        mtwnComputeTimingWork(&timing, x, false, &result);
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
