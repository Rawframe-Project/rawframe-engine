// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Windows input scope of a purpose.

#include "win32_scope.h"

int mwinWin32ScopeOf(mwinInputPurpose purpose)
{
    switch (purpose)
    {
    case mwin_purposeNumber:
        return mwin_scopeNumber;
    case mwin_purposeEmail:
        return mwin_scopeEmail;
    case mwin_purposePassword:
        return mwin_scopePassword;
    case mwin_purposeUrl:
        return mwin_scopeUrl;
    default:
        return mwin_scopeDefault;
    }
}
