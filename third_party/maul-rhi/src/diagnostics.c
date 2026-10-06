// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The diagnostic queue (mrhi-0027).

#include "diagnostics.h"

void mrhiInitDiagnostics(mrhiDiagnosticQueue* queue, mrhiDiagnostic* records, uint32_t capacity)
{
    queue->records = capacity == 0 ? nullptr : records;
    queue->capacity = capacity;
    queue->head = 0;
    queue->count = 0;
    atomic_flag_clear(&queue->lock);
}

static void Lock(mrhiDiagnosticQueue* queue)
{
    while (atomic_flag_test_and_set_explicit(&queue->lock, memory_order_acquire))
    {
    }
}

static void Unlock(mrhiDiagnosticQueue* queue)
{
    atomic_flag_clear_explicit(&queue->lock, memory_order_release);
}

void mrhiRecordDiagnostic(mrhiDiagnosticQueue* queue, mrhiDiagnosticCode code)
{
    mrhiDiagnostic* records = queue->records;
    if (records == nullptr)
    {
        return;
    }
    Lock(queue);
    uint32_t capacity = queue->capacity;
    uint32_t count = queue->count;
    mrhiDiagnostic* newest = &records[(queue->head + count + capacity - 1) % capacity];
    if (count > 0 && newest->code == code)
    {
        newest->count += newest->count < UINT32_MAX ? 1u : 0u;
    }
    else if (count < capacity)
    {
        records[(queue->head + count) % capacity] = (mrhiDiagnostic){.code = code, .count = 1};
        queue->count = count + 1;
    }
    Unlock(queue);
}

mrhiResult mrhiTakeDiagnostic(mrhiDiagnosticQueue* queue, mrhiDiagnostic* recordOut)
{
    const mrhiDiagnostic* records = queue->records;
    if (records == nullptr)
    {
        return mrhi_empty;
    }
    Lock(queue);
    mrhiResult status = mrhi_empty;
    if (queue->count > 0)
    {
        *recordOut = records[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        --queue->count;
        status = mrhi_success;
    }
    Unlock(queue);
    return status;
}
