// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 generic gamepads.

#include "win32_hid.h"

#include "allocator.h"

#include "maul-unicode/encoding.h"

#include <stddef.h>
#include <string.h>
#include <wchar.h>

// USB, as SDL names DirectInput devices in its database.
#define BUS_USB    0x0003
#define CAPS       32
#define PATH_UNITS 512

static void Load(HMODULE library, const char* name, void* function, size_t size)
{
    FARPROC found = GetProcAddress(library, name);
    memcpy(function, (const void*)&found, size);
}

#define LOAD(field, name) Load(library, name, (void*)&api->field, sizeof(api->field))

static bool LoadParser(mwinHidApi* api)
{
    HMODULE library = LoadLibraryW(L"hid.dll");
    *api = (mwinHidApi){.library = library,
                        .deviceList = GetRawInputDeviceList,
                        .deviceInfo = GetRawInputDeviceInfoW,
                        .inputData = GetRawInputData,
                        .openFile = CreateFileW,
                        .closeFile = CloseHandle};
    if (library == nullptr)
    {
        return false;
    }
    LOAD(getCaps, "HidP_GetCaps");
    LOAD(buttonCaps, "HidP_GetButtonCaps");
    LOAD(valueCaps, "HidP_GetValueCaps");
    LOAD(usages, "HidP_GetUsages");
    LOAD(usageValue, "HidP_GetUsageValue");
    LOAD(productString, "HidD_GetProductString");
    return api->getCaps != nullptr && api->buttonCaps != nullptr && api->valueCaps != nullptr &&
           api->usages != nullptr && api->usageValue != nullptr && api->productString != nullptr;
}

void mwinWin32HidStart(mwinWin32Hid* hid, mwinContext* context, uint64_t timeNs)
{
    *hid = (mwinWin32Hid){.context = context};
    if (!LoadParser(&hid->api))
    {
        mwinWin32HidStop(hid);
        return;
    }
    mwinWin32HidFind(hid, timeNs);
}

static mwinHidPad* Find(mwinWin32Hid* hid, HANDLE device)
{
    for (size_t i = 0; i < MWIN_WIN32_HID_PADS; i++)
    {
        if (hid->pads[i].device == device)
        {
            return &hid->pads[i];
        }
    }
    return nullptr;
}

// The usages or range of a capability, plain.
static void UsagesOf(BOOLEAN isRange, USAGE usage, USAGE minimum, USAGE maximum, mwinHidCap* cap)
{
    cap->first = isRange ? minimum : usage;
    cap->last = isRange ? maximum : usage;
}

static void ReadButtons(const mwinWin32Hid* hid, mwinHidPad* pad, USHORT count)
{
    HIDP_BUTTON_CAPS caps[CAPS];
    count = count < CAPS ? count : CAPS;
    if (hid->api.buttonCaps(HidP_Input, caps, &count, pad->preparsed) != HIDP_STATUS_SUCCESS)
    {
        return;
    }
    mwinHidCap plain[CAPS];
    for (USHORT i = 0; i < count; i++)
    {
        plain[i] = (mwinHidCap){.page = caps[i].UsagePage, .bits = 1};
        UsagesOf(caps[i].IsRange, caps[i].NotRange.Usage, caps[i].Range.UsageMin,
                 caps[i].Range.UsageMax, &plain[i]);
    }
    mwinHidAddButtons(&pad->hid, plain, count);
}

static void ReadValues(const mwinWin32Hid* hid, mwinHidPad* pad, USHORT count)
{
    HIDP_VALUE_CAPS caps[CAPS];
    count = count < CAPS ? count : CAPS;
    if (hid->api.valueCaps(HidP_Input, caps, &count, pad->preparsed) != HIDP_STATUS_SUCCESS)
    {
        return;
    }
    mwinHidCap plain[CAPS];
    for (USHORT i = 0; i < count; i++)
    {
        plain[i] = (mwinHidCap){.page = caps[i].UsagePage,
                                .bits = caps[i].BitSize,
                                .minimum = caps[i].LogicalMin,
                                .maximum = caps[i].LogicalMax};
        UsagesOf(caps[i].IsRange, caps[i].NotRange.Usage, caps[i].Range.UsageMin,
                 caps[i].Range.UsageMax, &plain[i]);
    }
    mwinHidAddValues(&pad->hid, plain, count);
}

// The device's controls, in the numbering; false for a device with none.
static bool ReadControls(const mwinWin32Hid* hid, mwinHidPad* pad)
{
    HIDP_CAPS caps;
    if (hid->api.getCaps(pad->preparsed, &caps) != HIDP_STATUS_SUCCESS)
    {
        return false;
    }
    ReadButtons(hid, pad, caps.NumberInputButtonCaps);
    ReadValues(hid, pad, caps.NumberInputValueCaps);
    const mwinPadControls* controls = &pad->hid.controls;
    return controls->buttonCount + controls->axisCount + controls->hatCount > 0;
}

// The product's name as UTF-8, or a plain one where the device gives
// none.
static void NameOf(const mwinWin32Hid* hid, const wchar_t* path, mwinGamepadInfo* info)
{
    wchar_t name[MWIN_GAMEPAD_NAME_BYTES / 2] = {0};
    HANDLE file = hid->api.openFile(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    bool named = file != INVALID_HANDLE_VALUE &&
                 hid->api.productString(file, name, sizeof(name) - sizeof(wchar_t));
    if (file != INVALID_HANDLE_VALUE)
    {
        (void)hid->api.closeFile(file);
    }
    size_t bytes = 0;
    if (!named || name[0] == L'\0' ||
        muniConvertUtf16ToUtf8((const uint16_t*)name, wcslen(name), muni_convertReplace, info->name,
                               sizeof(info->name), &bytes)
                .status != muni_success)
    {
        static const char plain[] = "HID gamepad";
        memcpy(info->name, plain, sizeof(plain) - 1);
        bytes = sizeof(plain) - 1;
    }
    info->nameLength = (uint32_t)bytes;
}

// The device's path, and whether it is one the backend reads: a
// joystick, gamepad or multi-axis controller that XInput does not.
static bool IsPad(const mwinWin32Hid* hid, HANDLE device, RID_DEVICE_INFO* info,
                  wchar_t path[PATH_UNITS])
{
    UINT size = sizeof(*info);
    info->cbSize = sizeof(*info);
    UINT units = PATH_UNITS;
    if (hid->api.deviceInfo(device, RIDI_DEVICEINFO, info, &size) == (UINT)-1 ||
        info->dwType != RIM_TYPEHID || info->hid.usUsagePage != HID_USAGE_PAGE_GENERIC ||
        (info->hid.usUsage != HID_USAGE_GENERIC_JOYSTICK &&
         info->hid.usUsage != HID_USAGE_GENERIC_GAMEPAD &&
         info->hid.usUsage != MWIN_HID_MULTI_AXIS) ||
        hid->api.deviceInfo(device, RIDI_DEVICENAME, path, &units) == (UINT)-1)
    {
        return false;
    }
    path[PATH_UNITS - 1] = L'\0';
    // XInput's own, which it reads.
    return wcsstr(path, L"IG_") == nullptr;
}

static void Release(mwinWin32Hid* hid, mwinHidPad* pad)
{
    if (pad->preparsed != nullptr)
    {
        mwinRelease(&hid->context->allocator, pad->preparsed, pad->preparsedBytes,
                    alignof(max_align_t));
    }
    *pad = (mwinHidPad){0};
}

// Keeps the device's HID parser data: false where it gives none.
static bool KeepParserData(mwinWin32Hid* hid, mwinHidPad* pad)
{
    UINT bytes = 0;
    if (hid->api.deviceInfo(pad->device, RIDI_PREPARSEDDATA, nullptr, &bytes) != 0 || bytes == 0)
    {
        return false;
    }
    pad->preparsed = mwinAllocate(&hid->context->allocator, bytes, alignof(max_align_t));
    pad->preparsedBytes = bytes;
    return pad->preparsed != nullptr &&
           hid->api.deviceInfo(pad->device, RIDI_PREPARSEDDATA, pad->preparsed, &bytes) ==
               pad->preparsedBytes;
}

void mwinWin32HidArrive(mwinWin32Hid* hid, HANDLE device, uint64_t timeNs)
{
    RID_DEVICE_INFO device_info;
    wchar_t path[PATH_UNITS];
    mwinHidPad* pad = Find(hid, nullptr);
    if (hid->api.getCaps == nullptr || device == nullptr || Find(hid, device) != nullptr ||
        !IsPad(hid, device, &device_info, path))
    {
        return;
    }
    if (pad == nullptr)
    {
        hid->refused = true;
        return;
    }
    pad->device = device;
    if (!KeepParserData(hid, pad) || !ReadControls(hid, pad))
    {
        Release(hid, pad);
        return;
    }
    const RID_DEVICE_INFO_HID* facts = &device_info.hid;
    const mwinPadDatabase* database = &mwinWindowsPadDatabase;
    const mwinPadMapping* mapping =
        mwinFindPadMapping(database, BUS_USB, (uint16_t)facts->dwVendorId,
                           (uint16_t)facts->dwProductId, (uint16_t)facts->dwVersionNumber);
    mwinPadControls* controls = &pad->hid.controls;
    controls->mapping = mapping;
    controls->halves = mapping != nullptr && mapping->halves != 0
                           ? database->halves[mapping->halves - 1]
                           : nullptr;
    int raw = controls->axisCount + 2 * controls->hatCount;
    mwinGamepadInfo info = {
        .vendor = (uint16_t)facts->dwVendorId,
        .product = (uint16_t)facts->dwProductId,
        .mapped = mapping != nullptr,
        .rawButtons = controls->buttonCount < MWIN_GAMEPAD_RAW_BUTTONS ? controls->buttonCount
                                                                       : MWIN_GAMEPAD_RAW_BUTTONS,
        .rawAxes = (uint8_t)(raw < MWIN_GAMEPAD_RAW_AXES ? raw : MWIN_GAMEPAD_RAW_AXES),
        .battery = -1};
    NameOf(hid, path, &info);
    int32_t slot = mwinAddGamepad(hid->context, &info, timeNs);
    if (slot < 0)
    {
        // Every slot is taken, or held by a pad whose removal the program
        // has not read.
        Release(hid, pad);
        hid->refused = true;
        return;
    }
    pad->slot = (uint32_t)slot;
}

void mwinWin32HidFind(mwinWin32Hid* hid, uint64_t timeNs)
{
    RAWINPUTDEVICELIST list[128];
    UINT count = sizeof(list) / sizeof(list[0]);
    UINT found = hid->api.deviceList != nullptr
                     ? hid->api.deviceList(list, &count, sizeof(RAWINPUTDEVICELIST))
                     : (UINT)-1;
    for (UINT i = 0; found != (UINT)-1 && i < found; i++)
    {
        if (list[i].dwType == RIM_TYPEHID)
        {
            mwinWin32HidArrive(hid, list[i].hDevice, timeNs);
        }
    }
}

void mwinWin32HidPump(mwinWin32Hid* hid, uint64_t timeNs)
{
    if (!hid->refused || Find(hid, nullptr) == nullptr)
    {
        return;
    }
    const mwinContext* context = hid->context;
    for (uint32_t i = 0; i < context->limits.gamepads; i++)
    {
        if (context->gamepads[i].status == mwin_slotFree)
        {
            hid->refused = false;
            mwinWin32HidFind(hid, timeNs);
            return;
        }
    }
}

void mwinWin32HidRemove(mwinWin32Hid* hid, HANDLE device, uint64_t timeNs)
{
    mwinHidPad* pad = device != nullptr ? Find(hid, device) : nullptr;
    if (pad != nullptr)
    {
        mwinRemoveGamepad(hid->context, pad->slot, timeNs);
        Release(hid, pad);
    }
}

// One input report: the buttons down, and each axis's and hat's value.
static void Parse(const mwinWin32Hid* hid, mwinHidPad* pad, BYTE* report, ULONG length)
{
    const mwinHidApi* api = &hid->api;
    const mwinPadControls* controls = &pad->hid.controls;
    USAGE down[MWIN_PAD_NUMBERED_BUTTONS];
    ULONG count = MWIN_PAD_NUMBERED_BUTTONS;
    if (api->usages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0, down, &count, pad->preparsed,
                    (PCHAR)report, length) == HIDP_STATUS_SUCCESS)
    {
        mwinHidSetButtons(&pad->hid, down,
                          count < MWIN_PAD_NUMBERED_BUTTONS ? count : MWIN_PAD_NUMBERED_BUTTONS);
    }
    for (uint32_t i = 0; i < (uint32_t)controls->axisCount + controls->hatCount; i++)
    {
        bool axis = i < controls->axisCount;
        USAGE usage = axis ? pad->hid.axes[i].usage : pad->hid.hats[i - controls->axisCount].usage;
        ULONG value = 0;
        if (api->usageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, usage, &value, pad->preparsed,
                            (PCHAR)report, length) == HIDP_STATUS_SUCCESS)
        {
            mwinHidSetValue(&pad->hid, i, value);
        }
    }
}

void mwinWin32HidInput(mwinWin32Hid* hid, HRAWINPUT input, uint64_t timeNs)
{
    union
    {
        RAWINPUT raw;
        BYTE bytes[1024];
    } data;
    UINT size = sizeof(data);
    if (hid->api.inputData == nullptr ||
        hid->api.inputData(input, RID_INPUT, &data, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1 ||
        data.raw.header.dwType != RIM_TYPEHID)
    {
        return;
    }
    mwinHidPad* pad = Find(hid, data.raw.header.hDevice);
    const RAWHID* reports = &data.raw.data.hid;
    size_t offset = offsetof(RAWINPUT, data.hid.bRawData);
    uint32_t count =
        mwinHidReports(size > offset ? size - offset : 0, reports->dwCount, reports->dwSizeHid);
    for (uint32_t i = 0; pad != nullptr && i < count; i++)
    {
        Parse(hid, pad, &data.bytes[offset + (size_t)i * reports->dwSizeHid], reports->dwSizeHid);
        mwinPostPadControls(hid->context, pad->slot, &pad->hid.controls, timeNs);
    }
}

void mwinWin32HidStop(mwinWin32Hid* hid)
{
    for (size_t i = 0; i < MWIN_WIN32_HID_PADS && hid->context != nullptr; i++)
    {
        Release(hid, &hid->pads[i]);
    }
    if (hid->api.library != nullptr)
    {
        (void)FreeLibrary(hid->api.library);
    }
    hid->api = (mwinHidApi){0};
}
