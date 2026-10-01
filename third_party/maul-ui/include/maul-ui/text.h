// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text service (record mui-0006): fonts, shaping and paragraphs, in
// the library when it is built with its text component (MAUL_UI_TEXT).
// The core reaches text only through host content, so a host may use
// another text stack instead.

#ifndef MAUL_UI_TEXT_H
#define MAUL_UI_TEXT_H

#include "maul-ui/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // The owner of fonts and text. A service is used by one thread at a
    // time, and is separate from any context, so one service serves many.
    typedef struct muiTextService muiTextService;

    // The named limits of a service. A request past one is refused with
    // mui_errorCapacity.
    typedef struct muiTextLimits
    {
        // Fonts and text blocks that exist at once. The service reserves
        // their records when it is created.
        uint32_t fonts;
        uint32_t textBlocks;
    } muiTextLimits;

    // How a service is made. Build it with muiDefaultTextServiceDef.
    typedef struct muiTextServiceDef
    {
        uint32_t cookie;
        // Gives the service's memory and FreeType's. HarfBuzz allocates
        // from the C library (record mui-0006).
        muiAllocator allocator;
        muiTextLimits limits;
    } muiTextServiceDef;

    /// Returns the default service def: 64 fonts, 1,024 text blocks and
    /// the C library's allocator.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiTextServiceDef muiDefaultTextServiceDef(void);

    /// Creates a text service.
    ///
    /// @param def         The service: a valid cookie, an allocator with both
    ///                    functions or neither, and a font limit from 1 to
    ///                    65,536.
    /// @param serviceOut  Receives the service; set to NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad
    ///         cookie, a half-set allocator or a limit out of range;
    ///         `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_NODISCARD MUI_API muiResult muiCreateTextService(const muiTextServiceDef* def,
                                                         muiTextService** serviceOut);

    /// Destroys a service and every font in it. Every id it gave out
    /// becomes meaningless.
    ///
    /// @param service  The service, or NULL for nothing.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_API void muiDestroyTextService(muiTextService* service);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TEXT_H
