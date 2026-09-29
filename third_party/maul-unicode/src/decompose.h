// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Decomposition and composition of single code points, over the
// generated normalization data (see src/tables.h), with the Hangul
// syllables computed as UAX #15 section 3.12 describes.

#ifndef MAUL_UNICODE_SRC_DECOMPOSE_H
#define MAUL_UNICODE_SRC_DECOMPOSE_H

#include <stddef.h>
#include <stdint.h>

// The most code points a full decomposition has: 18, for U+FDFA in NFKD.
#define MUNI_MAX_DECOMPOSITION 18

// The canonical mapping of codePoint, one level deep: two code points,
// or one with secondOut 0; false when it has none. Hangul syllables map
// to LV + T or L + V, as HarfBuzz expects.
bool muniCanonicalMapping(uint32_t codePoint, uint32_t* firstOut, uint32_t* secondOut);

// The primary composite of first and second, or 0 when there is none.
uint32_t muniComposeCanonical(uint32_t first, uint32_t second);

// Whether codePoint may combine with what precedes it (NFC_QC=Maybe).
bool muniCombinesBackward(uint32_t codePoint);

// Whether the full decomposition of codePoint uses a compatibility
// mapping somewhere, so NFKD differs from NFD for it.
bool muniUsesCompatibility(uint32_t codePoint);

// Whether codePoint has a canonical decomposition that NFC does not
// recompose: a singleton or a pair excluded from composition.
bool muniIsCompositionExcluded(uint32_t codePoint);

// Writes the full decomposition of codePoint (NFD, or NFKD with
// compatibility) into out, which holds MUNI_MAX_DECOMPOSITION code points,
// and returns its length, at least 1.
size_t muniDecomposeFully(uint32_t codePoint, bool compatibility, uint32_t* out);

#endif // MAUL_UNICODE_SRC_DECOMPOSE_H
