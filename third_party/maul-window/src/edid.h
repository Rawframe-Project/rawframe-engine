// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a monitor's EDID says about high dynamic range: the HDR static
// metadata data block of a CTA-861 extension (CTA-861.3), with the
// transfer functions the monitor takes and the luminances its maker
// gives for content: the most, the most averaged over a frame, and the
// least; and its image's size. The bytes come from outside (the
// registry on Win32, RandR on X11) and are read as hostile: every
// block's checksum, every length.

#ifndef MAUL_WINDOW_SRC_EDID_H
#define MAUL_WINDOW_SRC_EDID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mwinEdidHdr
{
    // The monitor takes SMPTE ST 2084 (PQ) or HLG signals.
    bool pq;
    bool hlg;
    // In nits, 0 where the block does not give them.
    float peakNits;
    float frameAverageNits;
    float minimumNits;
} mwinEdidHdr;

// Reads the HDR static metadata of an EDID: false when the bytes are
// no EDID or carry no such block.
bool mwinEdidHdrOf(const uint8_t* bytes, size_t length, mwinEdidHdr* hdrOut);

// Reads the image's size in millimeters: the preferred timing's, unless
// it strays more than a fifth from the size in centimeters the base
// block gives (some monitors put centimeters there), else that one;
// false, the size 0, where the EDID gives neither.
bool mwinEdidSizeOf(const uint8_t* bytes, size_t length, uint32_t* widthMmOut,
                    uint32_t* heightMmOut);

#endif // MAUL_WINDOW_SRC_EDID_H
