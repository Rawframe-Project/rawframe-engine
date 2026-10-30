// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Versions and result names.

#include "maul-tween/base.h"

mtwnVersion mtwnGetVersion(void)
{
    return (mtwnVersion){MTWN_VERSION_MAJOR, MTWN_VERSION_MINOR, MTWN_VERSION_PATCH};
}

const char* mtwnGetResultName(mtwnResult result)
{
    switch (result)
    {
    case mtwn_success:
        return "mtwn_success";
    case mtwn_errorInvalid:
        return "mtwn_errorInvalid";
    case mtwn_errorCapacity:
        return "mtwn_errorCapacity";
    case mtwn_errorStale:
        return "mtwn_errorStale";
    case mtwn_errorState:
        return "mtwn_errorState";
    case mtwn_errorVersion:
        return "mtwn_errorVersion";
    case mtwn_errorLimit:
        return "mtwn_errorLimit";
    case mtwn_errorBusy:
        return "mtwn_errorBusy";
    default:
        return "unknown result";
    }
}
