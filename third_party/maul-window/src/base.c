// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "maul-window/base.h"

mwinVersion mwinGetVersion(void)
{
    return (mwinVersion){MWIN_VERSION_MAJOR, MWIN_VERSION_MINOR, MWIN_VERSION_PATCH};
}

const char* mwinResultName(mwinResult result)
{
    switch (result)
    {
    case mwin_success:
        return "mwin_success";
    case mwin_empty:
        return "mwin_empty";
    case mwin_errorInvalid:
        return "mwin_errorInvalid";
    case mwin_errorCapacity:
        return "mwin_errorCapacity";
    case mwin_errorStale:
        return "mwin_errorStale";
    case mwin_errorUnsupported:
        return "mwin_errorUnsupported";
    case mwin_errorPlatform:
        return "mwin_errorPlatform";
    case mwin_errorState:
        return "mwin_errorState";
    case mwin_errorVersion:
        return "mwin_errorVersion";
    default:
        return "unknown result";
    }
}
