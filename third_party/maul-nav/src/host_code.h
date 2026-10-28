// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The host's code an owner object runs (its allocator, link
// generation's clearance test), marked while it runs: a call of the
// object made back in from there is refused with mnav_errorBusy and one
// misuse counted.
//
// The misuse count also counts calls refused with mnav_errorInvalid: an
// argument a correct program never passes counts one misuse on the first
// owner object the call is given, so a host can catch bugs in release
// builds. Bad tile bytes, refused input geometry and stale ids count none.
//
// Two counts, so that no call needs to know its thread. The allocator
// runs inside a call that changes the object, which no other call may
// run beside: every call that changes the object refuses while it runs.
// A clearance test runs inside link generation, which only reads the
// navmesh: a call that changes nothing a reader reads (staging a tile or
// its removal) stays allowed, from the test itself or from another
// thread, and every other call that changes the object refuses.

#ifndef MAUL_NAV_SRC_HOST_CODE_H
#define MAUL_NAV_SRC_HOST_CODE_H

#include "maul-nav/base.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct mnavHostCode
{
    // How many allocator calls, and how many clearance tests, run on the
    // object now: atomic, since link generation may run the clearance
    // test on several threads at once over one navmesh.
    _Atomic int32_t writing;
    _Atomic int32_t reading;
    // Calls refused as misuse, cumulative.
    _Atomic uint64_t misuse;
} mnavHostCode;

// Marks the object's allocator running, and its end; NULL marks nothing.
static inline void mnavHostEnter(mnavHostCode* host)
{
    if (host != nullptr)
    {
        atomic_fetch_add_explicit(&host->writing, 1, memory_order_acq_rel);
    }
}

static inline void mnavHostLeave(mnavHostCode* host)
{
    if (host != nullptr)
    {
        atomic_fetch_sub_explicit(&host->writing, 1, memory_order_acq_rel);
    }
}

// Marks a clearance test running over the object, and its end.
static inline void mnavHostEnterReader(mnavHostCode* host)
{
    atomic_fetch_add_explicit(&host->reading, 1, memory_order_acq_rel);
}

static inline void mnavHostLeaveReader(mnavHostCode* host)
{
    atomic_fetch_sub_explicit(&host->reading, 1, memory_order_acq_rel);
}

static inline bool mnavHostCountMisuse(mnavHostCode* host)
{
    atomic_fetch_add_explicit(&host->misuse, 1, memory_order_relaxed);
    return true;
}

// Whether a call that changes the object comes while host code of either
// kind runs on it: then one misuse is counted and the call refuses.
static inline bool mnavHostCalledBack(mnavHostCode* host)
{
    if (atomic_load_explicit(&host->writing, memory_order_acquire) == 0 &&
        atomic_load_explicit(&host->reading, memory_order_acquire) == 0)
    {
        return false;
    }
    return mnavHostCountMisuse(host);
}

// The same for a call that changes nothing a reader reads: refused only
// while the object's allocator runs.
static inline bool mnavHostCalledBackWriting(mnavHostCode* host)
{
    if (atomic_load_explicit(&host->writing, memory_order_acquire) == 0)
    {
        return false;
    }
    return mnavHostCountMisuse(host);
}

static inline uint64_t mnavHostMisuse(const mnavHostCode* host)
{
    return atomic_load_explicit(&host->misuse, memory_order_relaxed);
}

// The mark of an owner object, NULL for none; the object may be const,
// since the counts are the one part of it that every call may write.
#define MNAV_HOST(object) ((object) != nullptr ? (mnavHostCode*)&(object)->host : nullptr)

// The first of two marks that is not NULL: the object a call counts its
// misuse on is the first one it is given.
static inline mnavHostCode* mnavHostFirst(mnavHostCode* first, mnavHostCode* second)
{
    return first != nullptr ? first : second;
}

// Counts one misuse on the object when a call refused an argument as
// invalid, and returns the result; NULL, for no object, counts nothing.
static inline mnavResult mnavHostCountInvalid(mnavHostCode* host, mnavResult result)
{
    if (host != nullptr && result == mnav_errorInvalid)
    {
        mnavHostCountMisuse(host);
    }
    return result;
}

#endif // MAUL_NAV_SRC_HOST_CODE_H
