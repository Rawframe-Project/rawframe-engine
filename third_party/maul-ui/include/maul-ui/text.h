// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text service: fonts, shaping and paragraphs, in the library when it
// is built with its text component (MAUL_UI_TEXT). The core reaches text
// only through host content, so a host may use another text stack instead.
//
// A service is busy while one of its functions that use it runs, those
// given a muiTextHost among them, and an atlas while one of its
// functions that change it runs. Host code they call then (the
// allocator, FreeType's memory through it, the clipboard writer) calls
// none of those: one answers mui_errorBusy, or an empty answer where it
// returns no status, and counts one misuse. A call given a muiTextHost
// also keeps the host's context busy (maul-ui/context.h): host code it
// calls may read the context, and a change to it is refused with
// mui_errorBusy and one misuse counted on the context.

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

    // The largest limits a service takes.
    enum
    {
        MUI_MAX_FONTS = 65536,
        MUI_MAX_FONT_FAMILIES = 65536,
        MUI_MAX_TEXT_BLOCKS = 16777216
    };

    // The named limits of a service. A request past one is refused with
    // mui_errorCapacity.
    typedef struct muiTextLimits
    {
        // Fonts, text blocks and font families that exist at once. The
        // service reserves their records when it is created.
        uint32_t fonts;
        uint32_t textBlocks;
        uint32_t fontFamilies;
    } muiTextLimits;

    // How a service is made. Build it with muiDefaultTextServiceDef. The
    // cookie and the version come first, where every version keeps them.
    typedef struct muiTextServiceDef
    {
        uint32_t cookie;
        uint32_t version;
        // Gives the service's memory and FreeType's. HarfBuzz allocates
        // from the C library, since its allocator is one for the whole
        // process (docs/internals/text.md).
        muiAllocator allocator;
        muiTextLimits limits;
    } muiTextServiceDef;

// The cookie of a service def, which muiDefaultTextServiceDef sets and the
// service checks.
#define MUI_TEXT_SERVICE_DEF_COOKIE 0x6D757478u

    /// Returns the default service def: 64 fonts, 1,024 text blocks, 16
    /// font families and the C library's allocator.
    ///
    /// Built in the program from the headers it includes, so it carries
    /// their version (MUI_ABI_VERSION), which the service checks.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    static inline muiTextServiceDef muiDefaultTextServiceDef(void)
    {
#ifdef __cplusplus
        muiTextServiceDef def = {};
#else
    muiTextServiceDef def = {0};
#endif
        def.cookie = MUI_TEXT_SERVICE_DEF_COOKIE;
        def.version = MUI_ABI_VERSION;
        def.limits.fonts = 64;
        def.limits.textBlocks = 1024;
        def.limits.fontFamilies = 16;
        return def;
    }

    /// Creates a text service.
    ///
    /// @param def         The service: a valid cookie, an allocator with both
    ///                    functions or neither, a font limit from 1 to
    ///                    MUI_MAX_FONTS, a block limit up to
    ///                    MUI_MAX_TEXT_BLOCKS and a family limit up to
    ///                    MUI_MAX_FONT_FAMILIES.
    /// @param serviceOut  Receives the service; set to NULL on failure.
    /// @return `mui_success`; `mui_errorVersion` for a def built against
    ///         headers of another binary interface (MUI_ABI_VERSION), before
    ///         the rest of the def is read; `mui_errorInvalid` for a NULL
    ///         argument, a bad cookie, a half-set allocator or a font limit of
    ///         0; `mui_errorLimit` for a limit past its maximum;
    ///         `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_NODISCARD MUI_API muiResult muiCreateTextService(const muiTextServiceDef* def,
                                                         muiTextService** serviceOut);

    /// Destroys a service and every font in it. Every id it gave out
    /// becomes meaningless. A call while the service is busy destroys
    /// nothing and counts one misuse.
    ///
    /// @param service  The service, or NULL for nothing.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_API void muiDestroyTextService(muiTextService* service);

    /// Returns how many calls the service has refused as invalid input
    /// (`mui_errorInvalid`) or as calls back in while busy
    /// (`mui_errorBusy`), its glyph atlases' and text editing's among
    /// them: a count release builds can watch to catch a host's bugs.
    /// Stale ids are not misuse, and queries taking the service as const
    /// count nothing.
    ///
    /// @param service  The service.
    /// @return The count; 0 for a NULL service.
    /// @par Thread safety
    /// Safe from any thread; the service is used by one thread at a time.
    MUI_API uint64_t muiTextService_GetMisuse(const muiTextService* service);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TEXT_H
