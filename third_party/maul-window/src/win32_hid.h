// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 generic gamepads through Raw Input and the HID parser (window
// decision W12): the joysticks, gamepads and multi-axis controllers
// XInput does not read. Devices come and go with Raw Input's arrivals
// and removals, and their input reports are read as they come. Their
// controls are numbered as SDL numbers DirectInput's, so the Windows
// entries of SDL_GameControllerDB apply; a device the database lacks is
// raw. The functions are a table, so a test can stand in for a device.

#ifndef MAUL_WINDOW_SRC_WIN32_HID_H
#define MAUL_WINDOW_SRC_WIN32_HID_H

#include "core.h"
#include "pad_map.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h, whose types they use: NTSTATUS,
#include <winternl.h>
// the usages,
#include <hidusage.h>
// then the parser.
#include <hidpi.h>

#define MWIN_WIN32_HID_PADS 8
// The Generic Desktop usage of a multi-axis controller, which MinGW's
// headers do not name.
#define MWIN_HID_MULTI_AXIS 0x08

typedef struct mwinHidApi
{
    // hid.dll, NULL for a stand-in.
    HMODULE library;
    UINT(WINAPI* deviceList)(PRAWINPUTDEVICELIST list, PUINT count, UINT size);
    UINT(WINAPI* deviceInfo)(HANDLE device, UINT command, LPVOID data, PUINT size);
    UINT(WINAPI* inputData)(HRAWINPUT input, UINT command, LPVOID data, PUINT size, UINT header);
    HANDLE(WINAPI* openFile)(LPCWSTR path, DWORD access, DWORD share,
                             LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags,
                             HANDLE model);
    BOOL(WINAPI* closeFile)(HANDLE file);
    NTSTATUS(NTAPI* getCaps)(PHIDP_PREPARSED_DATA data, PHIDP_CAPS caps);
    NTSTATUS(NTAPI* buttonCaps)(HIDP_REPORT_TYPE type, PHIDP_BUTTON_CAPS caps, PUSHORT count,
                                PHIDP_PREPARSED_DATA data);
    NTSTATUS(NTAPI* valueCaps)(HIDP_REPORT_TYPE type, PHIDP_VALUE_CAPS caps, PUSHORT count,
                               PHIDP_PREPARSED_DATA data);
    NTSTATUS(NTAPI* usages)(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE list,
                            PULONG length, PHIDP_PREPARSED_DATA data, PCHAR report,
                            ULONG reportLength);
    NTSTATUS(NTAPI* usageValue)(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
                                PULONG value, PHIDP_PREPARSED_DATA data, PCHAR report,
                                ULONG reportLength);
    BOOLEAN(NTAPI* productString)(HANDLE file, PVOID buffer, ULONG length);
} mwinHidApi;

// A value control: an axis or a hat, its usage, and its range and size.
typedef struct mwinHidValue
{
    USAGE usage;
    USHORT bits;
    LONG minimum;
    LONG maximum;
} mwinHidValue;

typedef struct mwinHidPad
{
    // The device, NULL for a free entry, and the core's gamepad slot.
    HANDLE device;
    uint32_t slot;
    // The HID parser's data of the device, the context's memory.
    PHIDP_PREPARSED_DATA preparsed;
    size_t preparsedBytes;
    // The Button page's usages in the numbering's order, and the axes and
    // hats in theirs.
    USAGE buttons[MWIN_PAD_NUMBERED_BUTTONS];
    mwinHidValue axes[MWIN_PAD_NUMBERED_AXES];
    mwinHidValue hats[MWIN_PAD_NUMBERED_HATS];
    mwinPadControls controls;
} mwinHidPad;

typedef struct mwinWin32Hid
{
    mwinContext* context;
    mwinHidApi api;
    mwinHidPad pads[MWIN_WIN32_HID_PADS];
} mwinWin32Hid;

// Loads the HID parser and finds the devices present; without it there
// are no HID gamepads, and no error.
void mwinWin32HidStart(mwinWin32Hid* hid, mwinContext* context, uint64_t timeNs);

// Finds the devices present, through the table as it is.
void mwinWin32HidFind(mwinWin32Hid* hid, uint64_t timeNs);

// A device came or went (WM_INPUT_DEVICE_CHANGE), or sent a report
// (WM_INPUT).
void mwinWin32HidArrive(mwinWin32Hid* hid, HANDLE device, uint64_t timeNs);
void mwinWin32HidRemove(mwinWin32Hid* hid, HANDLE device, uint64_t timeNs);
void mwinWin32HidInput(mwinWin32Hid* hid, HRAWINPUT input, uint64_t timeNs);

// Lets every device go.
void mwinWin32HidStop(mwinWin32Hid* hid);

#endif // MAUL_WINDOW_SRC_WIN32_HID_H
