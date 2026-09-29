// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Identifier properties from their generated bit table.

#include "maul-unicode/identifier.h"

#include "encoding.h"
#include "tables.h"

enum
{
    BitStart = 1,
    BitContinue = 2,
    BitSyntax = 4,
    BitWhiteSpace = 8,
};

bool muniIsIdentifierStart(uint32_t codePoint)
{
    return (muniLookupIdentifier(codePoint) & BitStart) != 0;
}

bool muniIsIdentifierContinue(uint32_t codePoint)
{
    return (muniLookupIdentifier(codePoint) & BitContinue) != 0;
}

bool muniIsPatternSyntax(uint32_t codePoint)
{
    return (muniLookupIdentifier(codePoint) & BitSyntax) != 0;
}

bool muniIsPatternWhiteSpace(uint32_t codePoint)
{
    return (muniLookupIdentifier(codePoint) & BitWhiteSpace) != 0;
}

muniTextResult muniCheckIdentifier(const char* text, size_t length)
{
    if (text == nullptr && length != 0)
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    if (length == 0)
    {
        return (muniTextResult){muni_errorIdentifier, 0};
    }
    const uint8_t* bytes = (const uint8_t*)text;
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = muniStepUtf8(bytes + offset, length - offset, &codePoint, &size);
        if (status != muni_success)
        {
            return (muniTextResult){status, offset};
        }
        uint8_t needed = offset == 0 ? BitStart : BitContinue;
        if ((muniLookupIdentifier(codePoint) & needed) == 0)
        {
            return (muniTextResult){muni_errorIdentifier, offset};
        }
        offset += size;
    }
    return (muniTextResult){muni_success, length};
}
