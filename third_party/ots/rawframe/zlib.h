// zlib's one function OTS calls, refusing (D385): only WOFF 1.0 compresses
// tables, and the cook takes TrueType and OpenType files and collections,
// never WOFF (ADR-0049), so a compressed table fails the font.
#pragma once

typedef unsigned char Bytef;
typedef unsigned long uLong;
typedef unsigned long uLongf;

#define Z_OK 0
#define Z_DATA_ERROR (-3)

static inline int uncompress(Bytef* /*dest*/, uLongf* /*destLen*/, const Bytef* /*source*/, uLong /*sourceLen*/) {
    return Z_DATA_ERROR;
}
