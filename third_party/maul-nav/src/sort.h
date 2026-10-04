// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sorting 64-bit keys in linear time, the same on every platform.

#ifndef MAUL_NAV_SRC_SORT_H
#define MAUL_NAV_SRC_SORT_H

#include <stddef.h>
#include <stdint.h>

// Sorts keys ascending with a byte-wise radix sort; scratch holds count
// keys. Returns the number of distinct keys, which are left first.
size_t mnavSortUnique(uint64_t* keys, uint64_t* scratch, size_t count);

#endif // MAUL_NAV_SRC_SORT_H
