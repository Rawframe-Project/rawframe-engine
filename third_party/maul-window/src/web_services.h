// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web services: an address opened in a new tab, and the screen
// kept awake by a wake lock. A page shows no files, so revealing one
// is unsupported.

#ifndef MAUL_WINDOW_SRC_WEB_SERVICES_H
#define MAUL_WINDOW_SRC_WEB_SERVICES_H

#include "web.h"

// Opens the request's address in a new tab, which cannot reach the
// page back; a blocked popup is denied.
mwinOutcome mwinWebOpenUrl(const mwinRequest* request);

// Whether the page can keep the screen awake.
mwinOutcome mwinWebCanKeepAwake(void);

// Keeps the screen awake while some window that asks for it shows, or
// lets it go; each pump calls it. The page releases a wake lock when
// it is hidden, so the backend asks again once it shows; the page's
// end lets it go.
void mwinWebKeepAwake(mwinWebPlatform* platform, bool wanted);

#endif // MAUL_WINDOW_SRC_WEB_SERVICES_H
