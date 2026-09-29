// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test backend, a headless platform for contract tests. It exists
// only in builds with MAUL_WINDOW_TEST_BACKEND and only in a context
// created with mwin_backendTest; it is never a fallback. It answers each
// request at the next pump, as the answer set for its kind says, and
// takes what a platform would report from mwinTestPost, which sends it
// through the same path a real backend's reports take.

#ifndef MAUL_WINDOW_TEST_H
#define MAUL_WINDOW_TEST_H

#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/system.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Sets how the test platform answers requests of a kind from now on.
    /// mwin_outcomeDone, the default, carries a request out at the next
    /// pump.
    ///
    /// @param context  A context of the test backend.
    /// @param kind     The kind of request.
    /// @param outcome  The answer: any mwin_outcome value but superseded
    ///                 and cancelled, which only the core gives; cancelled
    ///                 too for file dialogs, as a user closing one.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context, an
    ///         unknown kind or an answer the platform cannot give.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetAnswer(mwinContext* context, mwinRequestKind kind,
                                                         mwinOutcome outcome);

    /// Holds requests: while held, none is answered.
    ///
    /// @param context  A context of the test backend.
    /// @param hold     true to hold, false to answer again from the next
    ///                 pump.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestHold(mwinContext* context, bool hold);

    /// Reports what the platform would: the user resized or closed a
    /// window, focus moved, the scale changed, a key went down, text was
    /// typed, the application is suspending. The report reaches the stream
    /// at the next pump, stamped with the time of this call; text is copied
    /// from the record's pointer. The pump runs a frame at once after a
    /// lifecycle or surface report, as a platform that waits for the
    /// program would make it; a lifecycle report needs no window.
    /// Completions, input state resets and the created and destroyed
    /// records are the core's and refused.
    ///
    /// @param context  A context of the test backend.
    /// @param event    The report; its window must be live, but for the
    ///                 lifecycle.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorCapacity` when 1,024 reports or
    ///         64 KiB of their text already wait; `mwin_errorUnsupported`
    ///         for a context of another backend; `mwin_errorInvalid` for a
    ///         NULL argument, a
    ///         record type only the core makes, or text that is not UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestPost(mwinContext* context, const mwinEvent* event);

    /// Sets the test platform's clock, which stamps every record.
    ///
    /// @param context  A context of the test backend.
    /// @param timeNs   Nanoseconds; it may not go back.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context or
    ///         a time before the current one.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetTime(mwinContext* context, uint64_t timeNs);

    /// Sets the scale the test platform gives windows it creates or
    /// resizes from now on; 1 at first.
    ///
    /// @param context  A context of the test backend.
    /// @param scale    A positive, finite scale.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context or a
    ///         scale that is not positive and finite.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetScale(mwinContext* context, float scale);

    /// Reads the title the test platform shows for a window.
    ///
    /// @param context    A context of the test backend.
    /// @param window     The window.
    /// @param buffer     Receives the title, not NUL-terminated. May be
    ///                   NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the title's length in bytes.
    /// @return `mwin_success`; `mwin_errorCapacity` when the title does not
    ///         fit (the bytes that fit are written); `mwin_errorStale` for
    ///         a window that no longer exists; `mwin_errorUnsupported` for
    ///         a context of another backend; `mwin_errorInvalid` for a NULL
    ///         argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGetTitle(const mwinContext* context,
                                                        mwinWindowId window, char* buffer,
                                                        size_t capacity, size_t* lengthOut);

    /// Connects a monitor to the test platform, at once: its
    /// mwin_eventMonitorAdded waits in the stream when this returns.
    /// Windows the platform makes from now on show on the primary monitor.
    ///
    /// @param context     A context of the test backend.
    /// @param info        The monitor's facts.
    /// @param monitorOut  Receives its id.
    /// @return `mwin_success`; `mwin_errorCapacity` when the context has
    ///         its limit of monitors; `mwin_errorUnsupported` for a context
    ///         of another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestAddMonitor(mwinContext* context,
                                                          const mwinMonitorInfo* info,
                                                          mwinMonitorId* monitorOut);

    /// Changes a connected monitor's facts, at once.
    ///
    /// @param context  A context of the test backend.
    /// @param monitor  The monitor.
    /// @param info     Its new facts.
    /// @return `mwin_success`; `mwin_errorStale` for a monitor no longer
    ///         connected; `mwin_errorUnsupported` for a context of another
    ///         backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestChangeMonitor(mwinContext* context,
                                                             mwinMonitorId monitor,
                                                             const mwinMonitorInfo* info);

    /// Disconnects a monitor, at once.
    ///
    /// @param context  A context of the test backend.
    /// @param monitor  The monitor.
    /// @return `mwin_success`; `mwin_errorStale` for a monitor no longer
    ///         connected; `mwin_errorUnsupported` for a context of another
    ///         backend; `mwin_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestRemoveMonitor(mwinContext* context,
                                                             mwinMonitorId monitor);

    /// Sets the system's preferences and facts, at once; a change of the
    /// look posts mwin_eventThemeChanged, of the power
    /// mwin_eventPowerChanged.
    ///
    /// @param context  A context of the test backend.
    /// @param facts    The facts.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetSystemFacts(mwinContext* context,
                                                              const mwinSystemFacts* facts);

    /// Sets the user's preferred locales, at once; a change posts
    /// mwin_eventLocaleChanged.
    ///
    /// @param context  A context of the test backend.
    /// @param locales  BCP 47 tags separated by commas, UTF-8. May be NULL
    ///                 when length is 0.
    /// @param length   The number of bytes.
    /// @return `mwin_success`; `mwin_errorCapacity` past the localeBytes
    ///         limit; `mwin_errorUnsupported` for a context of another
    ///         backend; `mwin_errorInvalid` for a NULL context or a list
    ///         that is not UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetLocales(mwinContext* context, const char* locales,
                                                          size_t length);

    /// Connects a gamepad to the test platform, at once: its
    /// mwin_eventGamepadAdded waits in the stream when this returns.
    ///
    /// @param context     A context of the test backend.
    /// @param info        The gamepad's facts.
    /// @param gamepadOut  Receives its id.
    /// @return `mwin_success`; `mwin_errorCapacity` when the context has
    ///         its limit of gamepads; `mwin_errorUnsupported` for a context
    ///         of another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestAddGamepad(mwinContext* context,
                                                          const mwinGamepadInfo* info,
                                                          mwinGamepadId* gamepadOut);

    /// Changes a connected gamepad's facts, at once.
    ///
    /// @param context  A context of the test backend.
    /// @param gamepad  The gamepad.
    /// @param info     Its new facts.
    /// @return `mwin_success`; `mwin_errorStale` for a gamepad no longer
    ///         connected; `mwin_errorUnsupported` for a context of another
    ///         backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestChangeGamepad(mwinContext* context,
                                                             mwinGamepadId gamepad,
                                                             const mwinGamepadInfo* info);

    /// Disconnects a gamepad, at once.
    ///
    /// @param context  A context of the test backend.
    /// @param gamepad  The gamepad.
    /// @return As mwinTestChangeGamepad, with `mwin_errorInvalid` for a
    ///         NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestRemoveGamepad(mwinContext* context,
                                                             mwinGamepadId gamepad);

    /// Presses or lets go a gamepad's button, at once; a button already so
    /// posts nothing.
    ///
    /// @param context  A context of the test backend.
    /// @param gamepad  The gamepad.
    /// @param button   An mwinGamepadButton, or a raw button's number.
    /// @param down     true to press it.
    /// @return As mwinTestRemoveGamepad.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGamepadButton(mwinContext* context,
                                                             mwinGamepadId gamepad, uint8_t button,
                                                             bool down);

    /// Moves a gamepad's axis, at once; a value it has already posts
    /// nothing.
    ///
    /// @param context  A context of the test backend.
    /// @param gamepad  The gamepad.
    /// @param axis     An mwinGamepadAxis, or a raw axis's number.
    /// @param value    Where it is.
    /// @return As mwinTestRemoveGamepad.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGamepadAxis(mwinContext* context,
                                                           mwinGamepadId gamepad, uint8_t axis,
                                                           float value);

    /// Reads the last rumble a gamepad of the test platform was given.
    ///
    /// @param context        A context of the test backend.
    /// @param gamepad        The gamepad.
    /// @param lowOut         Receives the low frequency motor's strength.
    /// @param highOut        Receives the high frequency motor's strength.
    /// @param durationMsOut  Receives the duration.
    /// @param countOut       Receives how many rumbles it was given.
    /// @return As mwinTestChangeGamepad.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGetRumble(const mwinContext* context,
                                                         mwinGamepadId gamepad, float* lowOut,
                                                         float* highOut, uint32_t* durationMsOut,
                                                         uint32_t* countOut);

    /// Puts bytes on the test platform's clipboard, at once, as another
    /// program would: they need not be UTF-8.
    ///
    /// @param context  A context of the test backend.
    /// @param bytes    The bytes. May be NULL when length is 0.
    /// @param length   Their number.
    /// @return `mwin_success`; `mwin_errorCapacity` when the allocator has
    ///         no room; `mwin_errorUnsupported` for a context of another
    ///         backend; `mwin_errorInvalid` for a NULL context, or NULL
    ///         bytes with a length.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetClipboard(mwinContext* context, const char* bytes,
                                                            size_t length);

    /// Puts UTF-16 on the test platform's clipboard, as Windows keeps it:
    /// it need not be well-formed.
    ///
    /// @param context  A context of the test backend.
    /// @param units    The code units. May be NULL when length is 0.
    /// @param length   Their number.
    /// @return As mwinTestSetClipboard.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetClipboardUtf16(mwinContext* context,
                                                                 const uint16_t* units,
                                                                 size_t length);

    /// Reads the test platform's clipboard as bytes: what the program last
    /// wrote, or what mwinTestSetClipboard put there.
    ///
    /// @param context    A context of the test backend.
    /// @param buffer     Receives the bytes. May be NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives their number.
    /// @return `mwin_success`; `mwin_errorCapacity` when they do not fit
    ///         (the bytes that fit are written); `mwin_errorUnsupported`
    ///         for a context of another backend; `mwin_errorInvalid` for a
    ///         NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGetClipboard(const mwinContext* context,
                                                            char* buffer, size_t capacity,
                                                            size_t* lengthOut);

    /// Drops files and text on a window of the test platform: gathered at
    /// once, delivered in order with the reports at the next pump. The
    /// drag's own records are reported with mwinTestPost.
    ///
    /// @param context      A context of the test backend.
    /// @param window       The window.
    /// @param position     Where, in the window.
    /// @param files        The paths as the platform gives them, each ended
    ///                     by a NUL; they need not be UTF-8. May be NULL
    ///                     when filesLength is 0.
    /// @param filesLength  Their bytes, NULs included.
    /// @param text         The text, which need not be UTF-8, or NULL for
    ///                     none.
    /// @param textLength   Its bytes.
    /// @return `mwin_success`; `mwin_errorState` while an earlier drop
    ///         waits; `mwin_errorCapacity` when 1,024 reports wait;
    ///         `mwin_errorStale` for a window that no longer exists;
    ///         `mwin_errorUnsupported` for a context of another backend;
    ///         `mwin_errorInvalid` for a NULL context, or files that are
    ///         NULL with a length or not ended by a NUL.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestDrop(mwinContext* context, mwinWindowId window,
                                                    mwinPosition position, const char* files,
                                                    size_t filesLength, const char* text,
                                                    size_t textLength);

    /// Reads the last address the test platform opened
    /// (mwin_requestOpenUrl) or path it revealed (mwin_requestRevealFile).
    ///
    /// @param context    A context of the test backend.
    /// @param kind       mwin_requestOpenUrl or mwin_requestRevealFile.
    /// @param buffer     Receives it. May be NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives its length in bytes, 0 before any.
    /// @return `mwin_success`; `mwin_errorCapacity` when it does not fit
    ///         (the bytes that fit are written); `mwin_errorUnsupported`
    ///         for a context of another backend; `mwin_errorInvalid` for a
    ///         NULL argument or another kind.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGetOpened(const mwinContext* context,
                                                         mwinRequestKind kind, char* buffer,
                                                         size_t capacity, size_t* lengthOut);

    /// Sets the paths the next file dialogs choose when they are done:
    /// each ended by a NUL. None makes a done dialog fail.
    ///
    /// @param context A context of the test backend.
    /// @param files   The paths. May be NULL when length is 0.
    /// @param length  Their bytes, at most 1024.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context, or
    ///         paths too long or not ended by a NUL.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestSetDialogFiles(mwinContext* context,
                                                              const char* files, size_t length);

    /// Copies out the last file dialog the backend was asked for, as
    /// lines: its kind as a digit, title, folder and name, then each
    /// filter as its name, ':' and its extensions.
    ///
    /// @param context   A context of the test backend.
    /// @param buffer    Receives it. May be NULL when capacity is 0.
    /// @param capacity  The bytes buffer holds.
    /// @param lengthOut Receives its length in bytes, 0 before any.
    /// @return As mwinTestGetOpened.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGetDialog(const mwinContext* context, char* buffer,
                                                         size_t capacity, size_t* lengthOut);

    /// Reads the icon the backend was last given: how many images, and a
    /// checksum of them in order, 64-bit FNV-1a over each image's width
    /// and height (4 bytes each, least significant first) and its
    /// pixels, packed.
    ///
    /// @param context     A context of the test backend.
    /// @param countOut    Receives the images, 0 before any icon.
    /// @param checksumOut Receives the checksum.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestGetIcon(const mwinContext* context,
                                                       uint32_t* countOut, uint64_t* checksumOut);

    /// Plays an accessibility client asking the window for its tree, as a
    /// screen reader would: the first time, mwin_eventAccessibilityRequested
    /// follows.
    ///
    /// @param context A context of the test backend.
    /// @param window  The window.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no longer
    ///         exists; `mwin_errorUnsupported` for a context of another
    ///         backend; `mwin_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinTestAskAccessibility(mwinContext* context,
                                                                mwinWindowId window);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_TEST_H
