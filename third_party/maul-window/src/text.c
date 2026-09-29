// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A circular buffer of bytes that never splits a reservation: when one
// does not fit before the end, it starts over at the beginning, and the
// bytes skipped at the end count as busy until the reservation after
// them is reclaimed. Records are drained in the order their bytes were
// reserved, so reclaiming moves the head forward to the end of the last
// drained reservation.

#include "text.h"

void* mwinReserveText(mwinTextRing* ring, uint32_t size, uint32_t align)
{
    if (ring->busy == 0)
    {
        ring->head = 0;
        ring->tail = 0;
    }
    if (ring->busy == ring->capacity)
    {
        return nullptr;
    }
    uint32_t start = (ring->tail + align - 1) & ~(align - 1);
    uint32_t skipped = start - ring->tail;
    if (ring->tail < ring->head)
    {
        if (start > ring->head || ring->head - start < size)
        {
            return nullptr;
        }
    }
    else if (start > ring->capacity || ring->capacity - start < size)
    {
        // The free space at the beginning, before head, must take it whole.
        if (ring->busy == 0 || ring->head < size)
        {
            return nullptr;
        }
        skipped = ring->capacity - ring->tail;
        start = 0;
    }
    ring->tail = start + size;
    ring->busy += skipped + size;
    return ring->bytes + start;
}

void mwinDrainText(mwinTextRing* ring, const void* start, const void* end)
{
    uint32_t first = (uint32_t)((const char*)start - ring->bytes);
    uint32_t last = (uint32_t)((const char*)end - ring->bytes);
    uint32_t from = ring->reclaimBytes > 0 ? ring->reclaimHead : ring->head;
    ring->reclaimBytes += first >= from ? last - from : ring->capacity - from + last;
    ring->reclaimHead = last;
}

void mwinReclaimText(mwinTextRing* ring)
{
    if (ring->reclaimBytes > 0)
    {
        ring->head = ring->reclaimHead;
        ring->busy -= ring->reclaimBytes;
        ring->reclaimBytes = 0;
    }
}

void mwinDropText(mwinTextRing* ring)
{
    ring->reclaimHead = ring->tail;
    ring->reclaimBytes = ring->busy;
}
