// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test backend, a headless platform for contract tests. It exists
// only in builds with MAUL_WINDOW_TEST_BACKEND and only in a context
// created with mwin_backendTest; it is never a fallback. It answers each
// request at the next pump, as the answer set for its kind says, and
// takes what a platform would report from mwinContext_PostTestEvent, which sends it
// through the same path a real backend's reports take.

#ifndef MAUL_WINDOW_TEST_H
#define MAUL_WINDOW_TEST_H

#include "maul-window/event.h"
#include "maul-window/input.h"
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
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestAnswer(mwinContext* context,
                                                                 mwinRequestKind kind,
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
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_HoldTestRequests(mwinContext* context,
                                                                    bool hold);

    /// Reports what the platform would: the user resized or closed a
    /// window, focus moved, the scale changed, a key went down, text was
    /// typed, the application is suspending. The report reaches the stream
    /// at the next pump, stamped with the time of this call; text is copied
    /// from the record's pointer, and a composition's offsets are fitted
    /// to its text as a platform's are. The pump runs a frame at once after a
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
    ///         for a context of another backend; `mwin_errorLimit` for a
    ///         composition of more than MWIN_MAX_PREEDIT_SEGMENTS
    ///         segments; `mwin_errorInvalid` for a NULL argument, a record
    ///         type only the core makes, or text that is not UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_PostTestEvent(mwinContext* context,
                                                                 const mwinEvent* event);

    /// Sets the test platform's clock, which stamps every record.
    ///
    /// @param context  A context of the test backend.
    /// @param timeNs   Nanoseconds; it may not go back.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context or
    ///         a time before the current one.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestTime(mwinContext* context,
                                                               uint64_t timeNs);

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
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestScale(mwinContext* context, float scale);

    /// Reads the title the test platform shows for a window.
    ///
    /// @param context    A context of the test backend.
    /// @param window     The window.
    /// @param bufferOut  Receives the title, not NUL-terminated. May be
    ///                   NULL when capacity is 0. Zeroed over capacity on
    ///                   failure, so a buffer too short holds none of it.
    /// @param capacity   The bytes bufferOut holds.
    /// @param lengthOut  Receives the title's length in bytes. On
    ///                   `mwin_errorCapacity`, the bytes needed; 0 on any
    ///                   other failure.
    /// @return `mwin_success`; `mwin_errorCapacity` when the title does not
    ///         fit; `mwin_errorStale` for a window that no longer exists;
    ///         `mwin_errorUnsupported` for a context of another backend;
    ///         `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_GetTestTitle(const mwinContext* context,
                                                               mwinWindowId window, char* bufferOut,
                                                               size_t capacity, size_t* lengthOut);

    /// Connects a monitor to the test platform, at once: its
    /// mwin_eventMonitorAdded waits in the stream when this returns.
    /// Windows the platform makes from now on show on the primary monitor.
    ///
    /// @param context     A context of the test backend.
    /// @param info        The monitor's facts.
    /// @param monitorOut  Receives its id. The null id on failure.
    /// @return `mwin_success`; `mwin_errorCapacity` when the context has
    ///         its limit of monitors; `mwin_errorUnsupported` for a context
    ///         of another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinAddTestMonitor(mwinContext* context,
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
    MWIN_NODISCARD MWIN_API mwinResult mwinMonitor_SetTestInfo(mwinContext* context,
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
    MWIN_NODISCARD MWIN_API mwinResult mwinRemoveTestMonitor(mwinContext* context,
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
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestSystemFacts(mwinContext* context,
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
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestLocales(mwinContext* context,
                                                                  const char* locales,
                                                                  size_t length);

    /// Connects a gamepad to the test platform, at once: its
    /// mwin_eventGamepadAdded waits in the stream when this returns.
    ///
    /// @param context     A context of the test backend.
    /// @param info        The gamepad's facts.
    /// @param gamepadOut  Receives its id. The null id on failure.
    /// @return `mwin_success`; `mwin_errorCapacity` when the context has
    ///         its limit of gamepads; `mwin_errorUnsupported` for a context
    ///         of another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinAddTestGamepad(mwinContext* context,
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
    MWIN_NODISCARD MWIN_API mwinResult mwinGamepad_SetTestInfo(mwinContext* context,
                                                               mwinGamepadId gamepad,
                                                               const mwinGamepadInfo* info);

    /// Disconnects a gamepad, at once.
    ///
    /// @param context  A context of the test backend.
    /// @param gamepad  The gamepad.
    /// @return As mwinGamepad_SetTestInfo, with `mwin_errorInvalid` for a
    ///         NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRemoveTestGamepad(mwinContext* context,
                                                             mwinGamepadId gamepad);

    /// Presses or lets go a gamepad's button, at once; a button already so
    /// posts nothing.
    ///
    /// @param context  A context of the test backend.
    /// @param gamepad  The gamepad.
    /// @param button   An mwinGamepadButton, or a raw button's number.
    /// @param down     true to press it.
    /// @return As mwinRemoveTestGamepad.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGamepad_SetTestButton(mwinContext* context,
                                                                 mwinGamepadId gamepad,
                                                                 uint8_t button, bool down);

    /// Moves a gamepad's axis, at once; a value it has already posts
    /// nothing.
    ///
    /// @param context  A context of the test backend.
    /// @param gamepad  The gamepad.
    /// @param axis     An mwinGamepadAxis, or a raw axis's number.
    /// @param value    Where it is.
    /// @return As mwinRemoveTestGamepad.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGamepad_SetTestAxis(mwinContext* context,
                                                               mwinGamepadId gamepad, uint8_t axis,
                                                               float value);

    /// Reads the last rumble a gamepad of the test platform was given.
    ///
    /// @param context        A context of the test backend.
    /// @param gamepad        The gamepad.
    /// @param lowOut         Receives the low frequency motor's strength.
    ///                       Zeroed on failure.
    /// @param highOut        Receives the high frequency motor's strength.
    ///                       Zeroed on failure.
    /// @param durationMsOut  Receives the duration. 0 on failure.
    /// @param countOut       Receives how many rumbles it was given. 0 on
    ///                       failure.
    /// @return As mwinGamepad_SetTestInfo.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGamepad_GetTestRumble(const mwinContext* context,
                                                                 mwinGamepadId gamepad,
                                                                 float* lowOut, float* highOut,
                                                                 uint32_t* durationMsOut,
                                                                 uint32_t* countOut);

    /// Reads the last trigger rumble a gamepad of the test platform was
    /// given.
    ///
    /// @param context        A context of the test backend.
    /// @param gamepad        The gamepad.
    /// @param leftOut        Receives the left trigger's strength. Zeroed
    ///                       on failure.
    /// @param rightOut       Receives the right trigger's strength. Zeroed
    ///                       on failure.
    /// @param durationMsOut  Receives the duration. 0 on failure.
    /// @param countOut       Receives how many trigger rumbles it was
    ///                       given. 0 on failure.
    /// @return As mwinGamepad_SetTestInfo.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGamepad_GetTestTriggerRumble(
        const mwinContext* context, mwinGamepadId gamepad, float* leftOut, float* rightOut,
        uint32_t* durationMsOut, uint32_t* countOut);

    /// Reads whether the test platform was told to turn a gamepad's motion
    /// sensors on.
    ///
    /// @param context  A context of the test backend.
    /// @param gamepad  The gamepad.
    /// @param onOut    Receives true while they are on. False on failure.
    /// @return As mwinGamepad_SetTestInfo.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGamepad_IsTestMotionEnabled(const mwinContext* context,
                                                                       mwinGamepadId gamepad,
                                                                       bool* onOut);

    /// Gives a gamepad's motion sensors a sample, at once, as the platform
    /// would; kept only while they are on.
    ///
    /// @param context       A context of the test backend.
    /// @param gamepad       The gamepad.
    /// @param acceleration  In m/s^2, gravity included.
    /// @param rotationRate  In radians per second.
    /// @param timeNs        When it was taken, on any steady clock.
    /// @return As mwinRemoveTestGamepad, with `mwin_errorInvalid` for a
    ///         NULL array.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGamepad_PostTestMotion(mwinContext* context,
                                                                  mwinGamepadId gamepad,
                                                                  const float acceleration[3],
                                                                  const float rotationRate[3],
                                                                  uint64_t timeNs);

    /// Sets what mwinContext_GetKeyReach answers for a chord on the test platform,
    /// which answers mwin_keyReachDelivered for every chord until set.
    /// Setting mwin_keyReachDelivered forgets the chord.
    ///
    /// @param context    A context of the test backend.
    /// @param code       The key.
    /// @param modifiers  The modifiers held with it; the lock bits are
    ///                   ignored.
    /// @param reach      The answer.
    /// @return `mwin_success`; `mwin_errorCapacity` when 32 chords are
    ///         already set; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL context, a
    ///         code that is not a key or an answer out of range.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestKeyReach(mwinContext* context,
                                                                   mwinKeyCode code,
                                                                   mwinModifiers modifiers,
                                                                   mwinKeyReach reach);

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
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestClipboard(mwinContext* context,
                                                                    const char* bytes,
                                                                    size_t length);

    /// Puts UTF-16 on the test platform's clipboard, as Windows keeps it:
    /// it need not be well-formed.
    ///
    /// @param context  A context of the test backend.
    /// @param units    The code units. May be NULL when length is 0.
    /// @param length   Their number.
    /// @return As mwinContext_SetTestClipboard.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestClipboardUtf16(mwinContext* context,
                                                                         const uint16_t* units,
                                                                         size_t length);

    /// Reads the test platform's clipboard as bytes: what the program last
    /// wrote, or what mwinContext_SetTestClipboard put there.
    ///
    /// @param context    A context of the test backend.
    /// @param bufferOut  Receives the bytes. May be NULL when capacity is
    ///                   0. Zeroed over capacity on failure, so a buffer
    ///                   too short holds none of it.
    /// @param capacity   The bytes bufferOut holds.
    /// @param lengthOut  Receives their number. On `mwin_errorCapacity`,
    ///                   the bytes needed; 0 on any other failure.
    /// @return `mwin_success`; `mwin_errorCapacity` when they do not fit;
    ///         `mwin_errorUnsupported` for a context of another backend;
    ///         `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_GetTestClipboard(const mwinContext* context,
                                                                    char* bufferOut,
                                                                    size_t capacity,
                                                                    size_t* lengthOut);

    /// Puts data of one MIME type on the test platform's clipboard, at
    /// once, as another program would: its text goes.
    ///
    /// @param context     A context of the test backend.
    /// @param mime        The type, 1 to MWIN_MAX_CLIPBOARD_MIME bytes, taken
    ///                    as it is.
    /// @param mimeLength  Its bytes.
    /// @param bytes       The data. May be NULL when length is 0.
    /// @param length      Its bytes.
    /// @return As mwinContext_SetTestClipboard, with `mwin_errorLimit` for a type
    ///         longer than MWIN_MAX_CLIPBOARD_MIME.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestClipboardData(mwinContext* context,
                                                                        const char* mime,
                                                                        size_t mimeLength,
                                                                        const void* bytes,
                                                                        size_t length);

    /// Reads the test platform's clipboard data of a MIME type, as the
    /// program last wrote it or mwinContext_SetTestClipboardData put it there.
    ///
    /// @param context     A context of the test backend.
    /// @param mime        The type, as written.
    /// @param mimeLength  Its bytes.
    /// @param bufferOut   Receives the bytes. May be NULL when capacity is
    ///                    0. Zeroed over capacity on failure, so a buffer
    ///                    too short holds none of it.
    /// @param capacity    The bytes bufferOut holds.
    /// @param lengthOut   Receives their number. On `mwin_errorCapacity`,
    ///                    the bytes needed; 0 on any other failure.
    /// @return As mwinContext_GetTestClipboard, with `mwin_errorInvalid` for a
    ///         type the clipboard does not hold.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_GetTestClipboardData(
        const mwinContext* context, const char* mime, size_t mimeLength, void* bufferOut,
        size_t capacity, size_t* lengthOut);

    /// Puts bytes in the test platform's primary selection, at once, as
    /// another program would: they need not be UTF-8.
    ///
    /// @param context  A context of the test backend.
    /// @param bytes    The bytes. May be NULL when length is 0.
    /// @param length   Their number.
    /// @return As mwinContext_SetTestClipboard.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestPrimary(mwinContext* context,
                                                                  const char* bytes, size_t length);

    /// Reads the test platform's primary selection as bytes.
    ///
    /// @param context    A context of the test backend.
    /// @param bufferOut  Receives the bytes. May be NULL when capacity is
    ///                   0. Zeroed over capacity on failure, so a buffer
    ///                   too short holds none of it.
    /// @param capacity   The bytes bufferOut holds.
    /// @param lengthOut  Receives their number. On `mwin_errorCapacity`,
    ///                   the bytes needed; 0 on any other failure.
    /// @return As mwinContext_GetTestClipboard.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_GetTestPrimary(const mwinContext* context,
                                                                  char* bufferOut, size_t capacity,
                                                                  size_t* lengthOut);

    /// Drops files and text on a window of the test platform: gathered at
    /// once, delivered in order with the reports at the next pump. The
    /// drag's own records are reported with mwinContext_PostTestEvent.
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
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_PostTestDrop(
        mwinContext* context, mwinWindowId window, mwinPosition position, const char* files,
        size_t filesLength, const char* text, size_t textLength);

    /// Reads the last address the test platform opened
    /// (mwin_requestOpenUrl) or path it revealed (mwin_requestRevealFile).
    ///
    /// @param context    A context of the test backend.
    /// @param kind       mwin_requestOpenUrl or mwin_requestRevealFile.
    /// @param bufferOut  Receives it. May be NULL when capacity is 0.
    ///                   Zeroed over capacity on failure, so a buffer too
    ///                   short holds none of it.
    /// @param capacity   The bytes bufferOut holds.
    /// @param lengthOut  Receives its length in bytes, 0 before any. On
    ///                   `mwin_errorCapacity`, the bytes needed; 0 on any
    ///                   other failure.
    /// @return `mwin_success`; `mwin_errorCapacity` when it does not fit;
    ///         `mwin_errorUnsupported` for a context of another backend;
    ///         `mwin_errorInvalid` for a NULL argument or another kind.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_GetTestOpened(const mwinContext* context,
                                                                 mwinRequestKind kind,
                                                                 char* bufferOut, size_t capacity,
                                                                 size_t* lengthOut);

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
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_SetTestDialogFiles(mwinContext* context,
                                                                      const char* files,
                                                                      size_t length);

    /// Copies out the last file dialog the backend was asked for, as
    /// lines: its kind as a digit, title, folder and name, then each
    /// filter as its name, ':' and its extensions.
    ///
    /// @param context    A context of the test backend.
    /// @param bufferOut  Receives it. May be NULL when capacity is 0.
    ///                   Zeroed over capacity on failure, so a buffer too
    ///                   short holds none of it.
    /// @param capacity   The bytes bufferOut holds.
    /// @param lengthOut  Receives its length in bytes, 0 before any. On
    ///                   `mwin_errorCapacity`, the bytes needed; 0 on any
    ///                   other failure.
    /// @return As mwinContext_GetTestOpened.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_GetTestDialog(const mwinContext* context,
                                                                 char* bufferOut, size_t capacity,
                                                                 size_t* lengthOut);

    /// Reads the icon the backend was last given: how many images, and a
    /// checksum of them in order, 64-bit FNV-1a over each image's width
    /// and height (4 bytes each, least significant first) and its
    /// pixels, packed.
    ///
    /// @param context     A context of the test backend.
    /// @param countOut    Receives the images, 0 before any icon. 0 on
    ///                    failure.
    /// @param checksumOut Receives the checksum. Zeroed on failure.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinContext_GetTestIcon(const mwinContext* context,
                                                               uint32_t* countOut,
                                                               uint64_t* checksumOut);

    /// Reads the cursor made from images a window shows, with the image it
    /// took for the test backend's scale.
    ///
    /// @param context    A context of the test backend.
    /// @param window     The window.
    /// @param cursorOut  Receives the cursor; a zero id while the window
    ///                   shows a shape. The null id on failure.
    /// @param imageOut   Receives the image's index. May be NULL. 0 on
    ///                   failure.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_GetTestCursor(const mwinContext* context,
                                                                mwinWindowId window,
                                                                mwinCursorId* cursorOut,
                                                                uint32_t* imageOut);

    /// Reads what a window last had carried out of its on-screen keyboard
    /// requests: whether the keyboard shows, and the purpose asked for.
    /// Hidden with mwin_purposeText before any.
    ///
    /// @param context     A context of the test backend.
    /// @param window      The window.
    /// @param visibleOut  Receives whether the keyboard shows.
    /// @param purposeOut  Receives the purpose of the text field.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult
    mwinWindow_GetTestVirtualKeyboard(const mwinContext* context, mwinWindowId window,
                                      bool* visibleOut, mwinInputPurpose* purposeOut);

    /// Reads what a window last had carried out of its text input
    /// requests: whether it accepts text, and the caret it gave, which a
    /// platform places candidate windows by. Not accepting,
    /// with an empty caret, before any.
    ///
    /// @param context     A context of the test backend.
    /// @param window      The window.
    /// @param enabledOut  Receives whether the window accepts text.
    /// @param caretOut    Receives the caret, in the window's logical
    ///                    units.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorUnsupported` for a context of
    ///         another backend; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_GetTestTextInput(const mwinContext* context,
                                                                   mwinWindowId window,
                                                                   bool* enabledOut,
                                                                   mwinRect* caretOut);

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
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_AskTestAccessibility(mwinContext* context,
                                                                       mwinWindowId window);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_TEST_H
