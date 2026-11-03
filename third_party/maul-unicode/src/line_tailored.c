// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Line breaking at the strict, normal and loose levels of CSS's
// line-break property, with the writing system: the rules of line_rules.h
// with the breaks each level adds. A file of its own, so that the default
// rules in line.c pay nothing for them.

#include "line_rules.h"

static uint8_t Lookup(const muniSegmenter* segmenter, uint32_t codePoint)
{
    return muniLineLookup(segmenter, codePoint, true);
}

static muniDecision Decide(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    return muniLineDecide(segmenter, value, codePoint, true);
}

muniResult muniNextLineTailoredSegment(muniSegmenter* segmenter, size_t* offsetOut)
{
    return muniSegmenterNext(segmenter, offsetOut, Lookup, Decide, Resolve, Absorb, nullptr);
}
