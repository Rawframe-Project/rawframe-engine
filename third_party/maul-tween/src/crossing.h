// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Crossings: how many times a sweep of a player's time
// passes a point deep in a clip, exactly, through every ancestor's
// iterations, without visiting them one by one.
//
// Each timeline's child time runs along a path: its iterations in the
// order its rate takes them, each from its start to its end. The points
// of a path are positions on it; a point below is crossed once each time
// the path covers it, an iteration's two ends both covered. The count of
// points a level has covered by a local time is worked out from the level
// below at one moving point plus constants each level keeps, so a count
// costs a walk down the chain.

#ifndef MAUL_TWEEN_SRC_CROSSING_H
#define MAUL_TWEEN_SRC_CROSSING_H

#include "maul-tween/clip.h"
#include "maul-tween/timing.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Crossings of a leaf: moving the same way as the coordinate counted in,
// and the other way.
typedef struct mtwnCrossings
{
    double same;
    double opposite;
} mtwnCrossings;

// What is counted, in the deepest level's child time: an instant there,
// or the iteration starts of a node beginning there (its iterations
// after the first, Level 1's current iteration changing).
typedef uint8_t mtwnLeafKind;
enum
{
    mtwn_leafInstant = 0,
    mtwn_leafIterations = 1,
};

typedef struct mtwnLeaf
{
    // The instant, or the node's start.
    double at;
    // The node's timing, its duration resolved, for iterations.
    mtwnTiming timing;
    // Its active duration when known (a sealed clip's node keeps it),
    // else worked out from the timing at each use.
    double activeDuration;
    mtwnLeafKind kind;
    bool activeKnown;
} mtwnLeaf;

// A timeline on the way down: its timing, its duration resolved, its
// start in its parent's child time, and what the preparation keeps.
typedef struct mtwnLevel
{
    mtwnTiming timing;
    double start;
    // Filled by mtwnPrepareLevels, unless known as filled (a finished
    // clip's node keeps it).
    double activeDuration;
    bool activeKnown;
    // Its iterations' shape, with the four below, kept for the counts.
    bool forwardRate;
    bool point;
    bool unbounded;
    double firstIteration;
    double firstProgress;
    double lastIteration;
    double lastProgress;
    // The coverage of the traversal's first iteration, of full forward
    // and reversed ones, and of the last, relative to the local time.
    mtwnCrossings cover[4];
    // Below at the iteration starts' child times (the first's, a forward
    // one's, a reversed one's), closed and strict.
    mtwnCrossings startBelow[3][2];
    // Below at the single point a zero-length level has.
    mtwnCrossings pointBelow;
} mtwnLevel;

// Prepares the levels, outermost first, for counting the leaf: each keeps
// counts of the levels below it.
void mtwnPrepareLevels(mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf);

// The outer levels prepared levels may be counted without: 1 for an
// outermost level passing its time through unchanged (endless, from 0,
// without delay, at rate 1, forwards) to the level or leaf below with
// nothing before its start, else 0. Counted from the level below, the counts are
// the same, as counts from it are none at or before 0.
uint32_t mtwnPassedLevels(const mtwnLevel* levels, uint32_t count);

// Counts the leaf's crossings as the outermost level's parent time sweeps
// from 'from' to 'to': (from, to] forwards, [to, from) backwards, closed
// at 'from' too when asked (a sweep from no time). The result's same is
// the crossings with the leaf moving forwards.
// The walks a count makes, remembered for the next counts of the same
// prepared levels and leaf: a search's fixed end is walked once. Each is
// the same bits worked out again; set count to 0 at each preparation.
typedef struct mtwnCrossingMemo
{
    uint32_t count;
    uint32_t next;
    double at[8];
    bool strict[8];
    mtwnCrossings reached[8];
} mtwnCrossingMemo;

// mtwnCountCrossings, its walks remembered in a memo (NULL for none).
mtwnCrossings mtwnCountCrossingsMemo(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                                     double from, double to, bool fromClosed,
                                     mtwnCrossingMemo* memo);

static inline mtwnCrossings mtwnCountCrossings(const mtwnLevel* levels, uint32_t count,
                                               const mtwnLeaf* leaf, double from, double to,
                                               bool fromClosed)
{
    return mtwnCountCrossingsMemo(levels, count, leaf, from, to, fromClosed, NULL);
}

// Which crossings a search counts.
typedef uint8_t mtwnCrossingWay;
enum
{
    mtwn_crossingEither = 0,
    mtwn_crossingForwards = 1,
    mtwn_crossingBackwards = 2,
};

// The time of the first (or last) crossing, of a way, in a sweep that has
// one: a double t where the count flips, the sweep to t (from t, for the
// last) counting one and to its neighbor toward 'from' (toward 'to')
// not, found by halving the doubles between, in their order as integers:
// at most 64 halvings, each a count. Where the count rises monotonically
// that is the double nearest the near end; its rounding can flip it more
// than once within a few doubles, and the halving's path then picks one.
// The search starts from a guess: a guess past the near end, up to
// the far one, is tested, and its neighbor toward the near end when it
// counts, and the halving goes on between whatever they leave. The time
// found flips the count as above; a right guess finds it in two counts.
// Where the count flips more than once the guess can pick another of
// those few doubles, the same one on every platform. NaN guesses nothing.
// Its counts' walks are remembered in a memo (NULL for none).
double mtwnFindCrossingFrom(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                            double from, double to, bool fromClosed, bool last, mtwnCrossingWay way,
                            double guess, mtwnCrossingMemo* memo);

// The search from no guess.
static inline double mtwnFindCrossing(const mtwnLevel* levels, uint32_t count, const mtwnLeaf* leaf,
                                      double from, double to, bool fromClosed, bool last,
                                      mtwnCrossingWay way)
{
    return mtwnFindCrossingFrom(levels, count, leaf, from, to, fromClosed, last, way, (double)NAN,
                                NULL);
}

// The iteration the last level is in at a time of the outermost level's
// parent, each level filling both ways.
double mtwnIterationAt(const mtwnLevel* levels, uint32_t count, double t);

#endif
