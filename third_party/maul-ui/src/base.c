// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "maul-ui/base.h"

muiVersion muiGetVersion(void)
{
    return (muiVersion){MUI_VERSION_MAJOR, MUI_VERSION_MINOR, MUI_VERSION_PATCH};
}

const char* muiGetResultName(muiResult result)
{
    switch (result)
    {
    case mui_success:
        return "mui_success";
    case mui_empty:
        return "mui_empty";
    case mui_errorInvalid:
        return "mui_errorInvalid";
    case mui_errorCapacity:
        return "mui_errorCapacity";
    case mui_errorStale:
        return "mui_errorStale";
    case mui_errorUnsupported:
        return "mui_errorUnsupported";
    case mui_errorPlatform:
        return "mui_errorPlatform";
    case mui_errorState:
        return "mui_errorState";
    case mui_errorVersion:
        return "mui_errorVersion";
    case mui_errorFormat:
        return "mui_errorFormat";
    case mui_errorLimit:
        return "mui_errorLimit";
    case mui_errorBusy:
        return "mui_errorBusy";
    default:
        return "unknown result";
    }
}
