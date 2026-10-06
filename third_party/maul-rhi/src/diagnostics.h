// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A diagnostic queue (mrhi-0027): the records of an instance's or a
// device's refusals, kept from the first until the program takes them,
// up to a capacity the program chose.

#ifndef MRHI_DIAGNOSTICS_H
#define MRHI_DIAGNOSTICS_H

#include "maul-rhi/base.h"

#include <stdatomic.h>

typedef struct mrhiDiagnosticQueue
{
    // The ring, capacity records; NULL exactly when the queue keeps none.
    mrhiDiagnostic* records;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    // Held while a record is added or taken, since refusals come from any
    // recording thread.
    atomic_flag lock;
} mrhiDiagnosticQueue;

// Makes a queue over room for capacity records (or none, at 0).
void mrhiInitDiagnostics(mrhiDiagnosticQueue* queue, mrhiDiagnostic* records, uint32_t capacity);

// Records a refusal by a check: a repeat of the newest record's check
// adds to its count, and a full queue drops it.
void mrhiRecordDiagnostic(mrhiDiagnosticQueue* queue, mrhiDiagnosticCode code);

// Takes the oldest record: success, or mrhi_empty.
mrhiResult mrhiTakeDiagnostic(mrhiDiagnosticQueue* queue, mrhiDiagnostic* recordOut);

#endif // MRHI_DIAGNOSTICS_H
