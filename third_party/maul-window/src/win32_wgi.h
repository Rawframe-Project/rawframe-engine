// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32's Xbox gamepads through Windows.Gaming.Input (mwin-0023): every
// Xbox-compatible pad, with no limit of four, its name, USB ids and
// battery, and its motors. The runtime is found through combase.dll
// when a context starts; where it is missing, XInput reads the pads
// (win32_pad.h). The runtime's interfaces are declared in the source
// rather than taken from the SDK's headers, whose C declarations differ
// between SDK versions, and only the methods called are named. The
// runtime says when a pad comes or goes on a thread of its own; the
// pads are read on the main thread, at each pump. The functions are a
// table, so a test can stand in for the runtime.

#ifndef MAUL_WINDOW_SRC_WIN32_WGI_H
#define MAUL_WINDOW_SRC_WIN32_WGI_H

#include "core.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// The buttons of a reading, as the runtime numbers them.
enum
{
    mwin_wgiMenu = 0x1,
    mwin_wgiView = 0x2,
    mwin_wgiA = 0x4,
    mwin_wgiB = 0x8,
    mwin_wgiX = 0x10,
    mwin_wgiY = 0x20,
    mwin_wgiDpadUp = 0x40,
    mwin_wgiDpadDown = 0x80,
    mwin_wgiDpadLeft = 0x100,
    mwin_wgiDpadRight = 0x200,
    mwin_wgiShoulderLeft = 0x400,
    mwin_wgiShoulderRight = 0x800,
    mwin_wgiStickLeft = 0x1000,
    mwin_wgiStickRight = 0x2000,
};

// A pad's controls: when the runtime read them (microseconds, moving
// with each report), its buttons, and its sticks and triggers as the
// runtime gives them, up positive.
typedef struct mwinWgiReading
{
    uint64_t timestamp;
    uint32_t buttons;
    double leftTrigger;
    double rightTrigger;
    double leftX;
    double leftY;
    double rightX;
    double rightY;
} mwinWgiReading;

// A pad's motors, each from 0 to 1: the heavy and light ones in the
// grips, and one in each trigger.
typedef struct mwinWgiMotors
{
    double low;
    double high;
    double leftTrigger;
    double rightTrigger;
} mwinWgiMotors;

// A pad is an opaque reference the table hands out; the same pad is the
// same pointer each time it is listed.
typedef struct mwinWgiApi
{
    void* self;
    // Lists the pads connected now, up to capacity, each with a reference
    // the caller releases; how many, or -1 when the runtime fails.
    int32_t (*list)(void* self, void** pads, uint32_t capacity);
    void (*release)(void* self, void* pad);
    bool (*read)(void* self, void* pad, mwinWgiReading* reading);
    bool (*vibrate)(void* self, void* pad, const mwinWgiMotors* motors);
    // Its name, vendor and product into info.
    void (*describe)(void* self, void* pad, mwinGamepadInfo* info);
    // Its battery's charge in percent, or -1 where it has none or says
    // nothing.
    int8_t (*battery)(void* self, void* pad);
    // Whether a pad came or went since the last call.
    bool (*changed)(void* self);
} mwinWgiApi;

typedef struct mwinWgiHandler mwinWgiHandler;

typedef struct mwinWgi
{
    HMODULE combase;
    // The runtime's functions, from combase.dll.
    HRESULT(WINAPI* activate)(void* classId, REFIID iid, void** factory);
    HRESULT(WINAPI* makeString)(const WCHAR* text, UINT32 length, void** string);
    HRESULT(WINAPI* deleteString)(void* string);
    const WCHAR*(WINAPI* stringText)(void* string, UINT32* length);
    // The gamepads' and raw controllers' statics, and the handler told
    // of arrivals and removals with its registrations for each.
    void* gamepads;
    void* controllers;
    mwinWgiHandler* handler;
    int64_t tokens[2];
    bool listening[2];
    // Whether this context initialized COM on its thread.
    bool com;
} mwinWgi;

// Finds the runtime and fills api with it; false, with nothing held,
// where there is none.
bool mwinWgiStart(mwinWgi* wgi, mwinWgiApi* api);
void mwinWgiStop(mwinWgi* wgi);

#endif // MAUL_WINDOW_SRC_WIN32_WGI_H
