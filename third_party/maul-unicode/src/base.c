// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Versions and result names.

#include "maul-unicode/base.h"

muniVersion muniGetVersion(void)
{
    return (muniVersion){MUNI_VERSION_MAJOR, MUNI_VERSION_MINOR, MUNI_VERSION_PATCH};
}

muniVersion muniGetUnicodeVersion(void)
{
    return (muniVersion){MUNI_UNICODE_VERSION_MAJOR, MUNI_UNICODE_VERSION_MINOR,
                         MUNI_UNICODE_VERSION_PATCH};
}

const char* muniResultName(muniResult result)
{
    switch (result)
    {
    case muni_success:
        return "muni_success";
    case muni_done:
        return "muni_done";
    case muni_needMoreText:
        return "muni_needMoreText";
    case muni_errorInvalid:
        return "muni_errorInvalid";
    case muni_errorCapacity:
        return "muni_errorCapacity";
    case muni_errorUtf8Lead:
        return "muni_errorUtf8Lead";
    case muni_errorUtf8Continuation:
        return "muni_errorUtf8Continuation";
    case muni_errorUtf8Truncated:
        return "muni_errorUtf8Truncated";
    case muni_errorUtf8Overlong:
        return "muni_errorUtf8Overlong";
    case muni_errorUtf8Surrogate:
        return "muni_errorUtf8Surrogate";
    case muni_errorUtf8TooLarge:
        return "muni_errorUtf8TooLarge";
    case muni_errorUtf16Surrogate:
        return "muni_errorUtf16Surrogate";
    case muni_errorLimit:
        return "muni_errorLimit";
    case muni_errorIdentifier:
        return "muni_errorIdentifier";
    case muni_errorUtf32Value:
        return "muni_errorUtf32Value";
    default:
        return "unknown result";
    }
}
