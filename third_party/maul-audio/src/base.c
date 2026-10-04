// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "maul-audio/base.h"

maudVersion maudGetVersion(void)
{
    return (maudVersion){MAUD_VERSION_MAJOR, MAUD_VERSION_MINOR, MAUD_VERSION_PATCH};
}

const char* maudResultName(maudResult result)
{
    switch (result)
    {
    case maud_success:
        return "maud_success";
    case maud_empty:
        return "maud_empty";
    case maud_errorInvalid:
        return "maud_errorInvalid";
    case maud_errorCapacity:
        return "maud_errorCapacity";
    case maud_errorStale:
        return "maud_errorStale";
    case maud_errorUnsupported:
        return "maud_errorUnsupported";
    case maud_errorPlatform:
        return "maud_errorPlatform";
    case maud_errorState:
        return "maud_errorState";
    default:
        return "unknown result";
    }
}
