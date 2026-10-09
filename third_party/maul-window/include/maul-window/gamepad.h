// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads, an optional component (MAUL_WINDOW_GAMEPAD, on by default):
// without it these functions are not in the library and a call to one
// fails to link.
//
// A gamepad the platform knows the layout of is mapped: its controls
// come by where they are on a standard pad (the south face button, the
// left stick), never by the letters printed on them. One it does not
// know comes raw, as numbered buttons and axes, its hats as pairs of
// axes. Each connection has its own id; mwin_eventGamepadAdded and
// mwin_eventGamepadRemoved report hotplug, mwin_eventGamepadChanged a
// change of its facts (its battery). Its buttons and axes come as
// records in the stream, whatever window has the focus, and its state
// can be read at any time. Values are the platform's own, with no dead
// zone: sticks run from -1 to 1, right and down positive, and triggers
// from 0 to 1.

#ifndef MAUL_WINDOW_GAMEPAD_H
#define MAUL_WINDOW_GAMEPAD_H

#include "maul-window/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // The most bytes of a gamepad's name.
#define MWIN_GAMEPAD_NAME_BYTES 64
    // The most buttons and axes a raw gamepad reports.
#define MWIN_GAMEPAD_RAW_BUTTONS 32
#define MWIN_GAMEPAD_RAW_AXES    16

    // A gamepad connection. Ids are never reused within a context.
    typedef struct mwinGamepadId
    {
        uint32_t index1;
        uint32_t generation;
    } mwinGamepadId;

    // The controls of a mapped gamepad, by where they are.
    typedef uint8_t mwinGamepadButton;

    enum
    {
        mwin_padDpadUp = 0,
        mwin_padDpadDown = 1,
        mwin_padDpadLeft = 2,
        mwin_padDpadRight = 3,
        mwin_padFaceSouth = 4,
        mwin_padFaceEast = 5,
        mwin_padFaceWest = 6,
        mwin_padFaceNorth = 7,
        mwin_padShoulderLeft = 8,
        mwin_padShoulderRight = 9,
        // A stick pressed in.
        mwin_padStickLeft = 10,
        mwin_padStickRight = 11,
        mwin_padStart = 12,
        mwin_padSelect = 13,
        mwin_padGuide = 14,
    };

#define MWIN_GAMEPAD_BUTTONS 15

    typedef uint8_t mwinGamepadAxis;

    enum
    {
        mwin_padStickLeftX = 0,
        mwin_padStickLeftY = 1,
        mwin_padStickRightX = 2,
        mwin_padStickRightY = 3,
        mwin_padTriggerLeft = 4,
        mwin_padTriggerRight = 5,
    };

#define MWIN_GAMEPAD_AXES 6

    // What a gamepad can do beyond its controls.
    typedef uint8_t mwinGamepadCapabilities;

    enum
    {
        // Low and high frequency motors (mwinSetGamepadRumble).
        mwin_padRumble = 1,
        // Motors in the triggers (mwinSetGamepadTriggerRumble).
        mwin_padTriggerRumble = 2,
        // Motion sensors (mwinSetGamepadMotion, mwinGetGamepadMotion).
        mwin_padMotion = 4,
    };

    // What the platform tells about a gamepad.
    typedef struct mwinGamepadInfo
    {
        // UTF-8, not NUL-terminated; a longer name, or one with ill-formed
        // bytes, is cut to the whole characters before the cut or the fault.
        char name[MWIN_GAMEPAD_NAME_BYTES];
        uint32_t nameLength;
        // The USB vendor and product ids, 0 where unknown.
        uint16_t vendor;
        uint16_t product;
        // Its controls come mapped; otherwise raw, as many as these.
        bool mapped;
        uint8_t rawButtons;
        uint8_t rawAxes;
        mwinGamepadCapabilities capabilities;
        // The battery's charge in percent, -1 where unknown or wired.
        int8_t battery;
    } mwinGamepadInfo;

    // A gamepad's controls as the platform last reported them.
    typedef struct mwinGamepadState
    {
        // Bit b set while button b is held: mwinGamepadButton of a mapped
        // gamepad, the raw button's number of a raw one.
        uint32_t buttons;
        // mwinGamepadAxis of a mapped gamepad (the rest 0), the raw axes of
        // a raw one.
        float axes[MWIN_GAMEPAD_RAW_AXES];
    } mwinGamepadState;

    // A gamepad's motion as its sensors report it, for a gamepad held in
    // front of the player: x to the right, y up, z toward the player
    // (mwin-0028).
    typedef struct mwinGamepadMotion
    {
        // The latest acceleration in m/s^2, gravity included: a gamepad
        // lying still reads about 9.8 along y.
        float acceleration[3];
        // The latest rotation rate in radians per second about each axis,
        // counter-clockwise positive.
        float rotationRate[3];
        // The angle turned about each axis since the last read, in
        // radians: every sample's rate over its time, summed.
        float rotation[3];
        // When the latest sample was taken, on the records' clock; 0
        // before the first.
        uint64_t timeNs;
    } mwinGamepadMotion;

    // A button of a gamepad pressed or let go (data.gamepadButton).
    typedef struct mwinGamepadButtonEvent
    {
        mwinGamepadId gamepad;
        // An mwinGamepadButton, or the raw button's number.
        uint8_t button;
        bool raw;
    } mwinGamepadButtonEvent;

    // An axis of a gamepad moved (data.gamepadAxis).
    typedef struct mwinGamepadAxisEvent
    {
        mwinGamepadId gamepad;
        // An mwinGamepadAxis, or the raw axis's number.
        uint8_t axis;
        bool raw;
        float value;
    } mwinGamepadAxisEvent;

    /// Lists the gamepads connected now, in the order they came.
    ///
    /// @param context   The context.
    /// @param gamepads  Receives up to capacity ids. May be NULL when
    ///                  capacity is 0.
    /// @param capacity  The ids gamepads holds.
    /// @param countOut  Receives the number connected, which may exceed
    ///                  capacity.
    /// @return `mwin_success`; `mwin_errorCapacity` when they do not all
    ///         fit (the first capacity are written); `mwin_errorInvalid`
    ///         for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetGamepads(const mwinContext* context,
                                                       mwinGamepadId* gamepads, size_t capacity,
                                                       size_t* countOut);

    /// Reads what the platform tells about a gamepad.
    ///
    /// @param context  The context.
    /// @param gamepad  The gamepad.
    /// @param infoOut  Receives its facts.
    /// @return `mwin_success`; `mwin_errorStale` for a gamepad no longer
    ///         connected; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetGamepadInfo(const mwinContext* context,
                                                          mwinGamepadId gamepad,
                                                          mwinGamepadInfo* infoOut);

    /// Reads a gamepad's controls as the platform last reported them,
    /// which may be ahead of the records not yet drained. After a
    /// mwin_eventInputStateReset about a gamepad, this is where its state
    /// is.
    ///
    /// @param context   The context.
    /// @param gamepad   The gamepad.
    /// @param stateOut  Receives its state.
    /// @return As mwinGetGamepadInfo.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetGamepadState(const mwinContext* context,
                                                           mwinGamepadId gamepad,
                                                           mwinGamepadState* stateOut);

    /// Runs a gamepad's motors, the low frequency (heavy) one and the high
    /// frequency (light) one, each from 0 (still) to 1, for a time or
    /// until the next call: the latest call wins, and a duration of 0
    /// stops them.
    ///
    /// @param context     The context.
    /// @param gamepad     The gamepad.
    /// @param low         The low frequency motor's strength.
    /// @param high        The high frequency motor's strength.
    /// @param durationMs  How long, in milliseconds.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a gamepad
    ///         without mwin_padRumble; `mwin_errorPlatform` when the
    ///         platform refused; `mwin_errorStale` for a gamepad no longer
    ///         connected; `mwin_errorInvalid` for a NULL context or a
    ///         strength outside 0 to 1.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinSetGamepadRumble(mwinContext* context,
                                                            mwinGamepadId gamepad, float low,
                                                            float high, uint32_t durationMs);

    /// Runs the motors in a gamepad's triggers, each from 0 (still) to 1,
    /// for a time or until the next call, as mwinSetGamepadRumble runs the
    /// others: the latest call wins, a duration of 0 stops them, and the
    /// two calls do not touch each other's motors.
    ///
    /// @param context     The context.
    /// @param gamepad     The gamepad.
    /// @param left        The left trigger's motor's strength.
    /// @param right       The right trigger's motor's strength.
    /// @param durationMs  How long, in milliseconds.
    /// @return As mwinSetGamepadRumble, with `mwin_errorUnsupported` for a
    ///         gamepad without mwin_padTriggerRumble.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinSetGamepadTriggerRumble(mwinContext* context,
                                                                   mwinGamepadId gamepad,
                                                                   float left, float right,
                                                                   uint32_t durationMs);

    /// Turns a gamepad's motion sensors on or off. They are off when it
    /// connects, as they cost its battery and its link; turning them
    /// either way starts its motion anew, all zero.
    ///
    /// @param context  The context.
    /// @param gamepad  The gamepad.
    /// @param enabled  true to turn them on.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a gamepad
    ///         without mwin_padMotion; `mwin_errorPlatform` when the
    ///         platform refused; `mwin_errorStale` for a gamepad no longer
    ///         connected; `mwin_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinSetGamepadMotion(mwinContext* context,
                                                            mwinGamepadId gamepad, bool enabled);

    /// Reads a gamepad's motion and starts the angle turned anew: the
    /// latest acceleration and rotation rate, and the angle turned since
    /// the last read. All zero while its sensors are off or before their
    /// first sample.
    ///
    /// @param context    The context.
    /// @param gamepad    The gamepad.
    /// @param motionOut  Receives its motion.
    /// @return `mwin_success`; `mwin_errorUnsupported` for a gamepad
    ///         without mwin_padMotion; `mwin_errorStale` for a gamepad no
    ///         longer connected; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetGamepadMotion(mwinContext* context,
                                                            mwinGamepadId gamepad,
                                                            mwinGamepadMotion* motionOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_GAMEPAD_H
