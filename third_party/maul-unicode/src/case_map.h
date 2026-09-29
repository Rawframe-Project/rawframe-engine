// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The case mappings of single code points, over the generated case data
// (see src/tables.h), for the modules that map text.

#ifndef MAUL_UNICODE_SRC_CASE_MAP_H
#define MAUL_UNICODE_SRC_CASE_MAP_H

#include "tables.h"

#include <stddef.h>
#include <stdint.h>

// The order of the mappings in a record and in the special table.
enum
{
    muni_caseKindUpper = 0,
    muni_caseKindLower = 1,
    muni_caseKindTitle = 2,
    muni_caseKindFold = 3,
};

// The most code points a full mapping has.
#define MUNI_MAX_CASE_MAPPING 3

// The case record of codePoint: four indexes into muniCaseDeltas, one
// per kind, then the muni_caseFlag bits.
static inline const uint8_t* muniCaseRecord(uint32_t codePoint)
{
    return muniCaseRecords[muniLookupCase(codePoint)];
}

// Writes the full mapping of a kind of a code point the case record
// flags special into out, and returns its length, which may be 0.
size_t muniMapCaseSpecial(uint32_t codePoint, int kind, uint32_t* out);

// Writes the full mapping of a kind of codePoint, the one that needs no
// context, into out, which holds MUNI_MAX_CASE_MAPPING code points, and
// returns its length: 1 or more, or 0 for a mapping to nothing.
static inline size_t muniMapCaseFully(uint32_t codePoint, int kind, uint32_t* out)
{
    const uint8_t* record = muniCaseRecord(codePoint);
    if ((record[4] & muni_caseFlagSpecial) != 0)
    {
        return muniMapCaseSpecial(codePoint, kind, out);
    }
    out[0] = (uint32_t)((int32_t)codePoint + muniCaseDeltas[record[kind]]);
    return 1;
}

#endif // MAUL_UNICODE_SRC_CASE_MAP_H
