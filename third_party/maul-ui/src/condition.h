// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Conditions as data: whether one is well formed, what it reads, what
// its values may therefore not set, and whether it holds for a sample of
// a node's last layout and the environment (record mui-0004).

#ifndef MAUL_UI_SRC_CONDITION_H
#define MAUL_UI_SRC_CONDITION_H

#include "property_bits.h"

#include "maul-ui/style.h"

#include <stdbool.h>

// What a condition reads of its node's last layout, as bits.
typedef uint8_t muiConditionReads;

enum
{
    mui_readsSize = 1,
    mui_readsDirection = 2,
};

// What a condition is tested against.
typedef struct muiConditionSample
{
    // The node's border box and resolved direction from its last layout.
    float width;
    float height;
    bool rtl;
    const muiEnvironment* environment;
} muiConditionSample;

bool muiIsConditionValid(const muiCondition* condition);

bool muiIsEnvironmentValid(const muiEnvironment* environment);

muiConditionReads muiConditionReadsOf(const muiCondition* condition);

// The properties a condition's values may not set: those it reads.
muiPropertyBits muiForbiddenProperties(const muiCondition* condition);

bool muiConditionHolds(const muiCondition* condition, const muiConditionSample* sample);

#endif // MAUL_UI_SRC_CONDITION_H
