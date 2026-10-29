// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A host's task system may run a range on the thread that enqueues or
// waits, inside the hooks; the range is then an outermost entry of its
// own, since the hooks run outside the caller's entry.

#include "host_tasks.h"

#include "fpenv.h"

// The work every range shares.
typedef struct Work
{
    maudTaskFn* task;
    void* context;
} Work;

// One range of it.
typedef struct Range
{
    const Work* work;
    uint32_t start;
    uint32_t end;
} Range;

static void RunRange(void* context)
{
    const Range* r = context;
    r->work->task(r->start, r->end, r->work->context);
}

// A range as the hooks run it.
static void InFloatEnv(uint32_t start, uint32_t end, void* context)
{
    Range range = {context, start, end};
    maudFloatEnvRun(RunRange, &range);
}

static MAUD_NOINLINE void* Enqueue(const maudHostTasks* hooks, uint32_t items, Work* work)
{
    maudFloatHost host = maudFloatHostEnter();
    void* handle = hooks->enqueue(InFloatEnv, items, 1, work, hooks->user);
    maudFloatHostLeave(host);
    return handle;
}

static MAUD_NOINLINE void Finish(const maudHostTasks* hooks, void* handle)
{
    maudFloatHost host = maudFloatHostEnter();
    hooks->finish(handle, hooks->user);
    maudFloatHostLeave(host);
}

void maudRunHostTasks(const maudHostTasks* hooks, maudTaskFn* task, uint32_t items, void* context)
{
    if (items == 0)
    {
        return;
    }
    if (hooks->enqueue == nullptr)
    {
        task(0, items, context);
        return;
    }
    Work work = {task, context};
    Finish(hooks, Enqueue(hooks, items, &work));
}
