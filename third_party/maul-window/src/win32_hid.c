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
#define BUS_USB 0x0003
// Generic Desktop's usages: the six axes from X, the sliders, and the
// hat switch.
#define USAGE_X      0x30
#define USAGE_RZ     0x35
#define USAGE_SLIDER 0x36
#define USAGE_WHEEL  0x38
#define USAGE_HAT    0x39
#define CAPS         32
#define PATH_UNITS   512

// A hat's eight steps from up, clockwise, as direction bits.
static const uint8_t s_hatBits[8] = {1, 1 | 2, 2, 2 | 4, 4, 4 | 8, 8, 8 | 1};

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

// Adds a Button page usage in order, once.
static void AddButton(mwinHidPad* pad, USAGE usage)
{
    uint8_t* count = &pad->controls.buttonCount;
    uint8_t at = 0;
    while (at < *count && pad->buttons[at] < usage)
    {
        at++;
    }
    if ((at < *count && pad->buttons[at] == usage) || *count == MWIN_PAD_NUMBERED_BUTTONS)
    {
        return;
    }
    memmove(&pad->buttons[at + 1], &pad->buttons[at], (*count - at) * sizeof(USAGE));
    pad->buttons[at] = usage;
    *count += 1;
}

static void ReadButtons(const mwinWin32Hid* hid, mwinHidPad* pad, USHORT count)
{
    HIDP_BUTTON_CAPS caps[CAPS];
    count = count < CAPS ? count : CAPS;
    if (hid->api.buttonCaps(HidP_Input, caps, &count, pad->preparsed) != HIDP_STATUS_SUCCESS)
    {
        return;
    }
    for (USHORT i = 0; i < count; i++)
    {
        USAGE first = caps[i].IsRange ? caps[i].Range.UsageMin : caps[i].NotRange.Usage;
        USAGE last = caps[i].IsRange ? caps[i].Range.UsageMax : caps[i].NotRange.Usage;
        for (uint32_t usage = first; caps[i].UsagePage == HID_USAGE_PAGE_BUTTON && usage <= last;
             usage++)
        {
            AddButton(pad, (USAGE)usage);
        }
    }
}

// A value control of a capability: a range whose maximum reads as
// negative is the unsigned range of its bits.
static mwinHidValue ValueOf(const HIDP_VALUE_CAPS* caps, USAGE usage)
{
    mwinHidValue value = {usage, caps->BitSize, caps->LogicalMin, caps->LogicalMax};
    if (value.maximum < value.minimum && value.bits < 32)
    {
        value.minimum = 0;
        value.maximum = (LONG)((1ul << value.bits) - 1);
    }
    return value;
}

// The axes and hats, numbered as DirectInput's: the six axes from X in
// usage order, then up to two sliders, dials or wheels; the hats apart.
static void ReadValues(const mwinWin32Hid* hid, mwinHidPad* pad, USHORT count)
{
    HIDP_VALUE_CAPS caps[CAPS];
    count = count < CAPS ? count : CAPS;
    if (hid->api.valueCaps(HidP_Input, caps, &count, pad->preparsed) != HIDP_STATUS_SUCCESS)
    {
        return;
    }
    mwinHidValue axes[USAGE_RZ - USAGE_X + 3] = {0};
    uint8_t sliders = 0;
    for (USHORT i = 0; i < count; i++)
    {
        USAGE first = caps[i].IsRange ? caps[i].Range.UsageMin : caps[i].NotRange.Usage;
        USAGE last = caps[i].IsRange ? caps[i].Range.UsageMax : caps[i].NotRange.Usage;
        for (uint32_t usage = first; caps[i].UsagePage == HID_USAGE_PAGE_GENERIC && usage <= last;
             usage++)
        {
            mwinHidValue value = ValueOf(&caps[i], (USAGE)usage);
            if (usage >= USAGE_X && usage <= USAGE_RZ)
            {
                axes[usage - USAGE_X] = value;
            }
            else if (usage >= USAGE_SLIDER && usage <= USAGE_WHEEL && sliders < 2)
            {
                axes[USAGE_RZ - USAGE_X + 1 + sliders++] = value;
            }
            else if (usage == USAGE_HAT && pad->controls.hatCount < MWIN_PAD_NUMBERED_HATS)
            {
                pad->hats[pad->controls.hatCount++] = value;
            }
        }
    }
    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++)
    {
        if (axes[i].usage != 0)
        {
            pad->axes[pad->controls.axisCount++] = axes[i];
        }
    }
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
    const mwinPadControls* controls = &pad->controls;
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
        pad == nullptr || !IsPad(hid, device, &device_info, path))
    {
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
    mwinPadControls* controls = &pad->controls;
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
        Release(hid, pad);
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

void mwinWin32HidRemove(mwinWin32Hid* hid, HANDLE device, uint64_t timeNs)
{
    mwinHidPad* pad = device != nullptr ? Find(hid, device) : nullptr;
    if (pad != nullptr)
    {
        mwinRemoveGamepad(hid->context, pad->slot, timeNs);
        Release(hid, pad);
    }
}

// A value's bits as the signed number a range below 0 means.
static LONG Extend(ULONG value, const mwinHidValue* control)
{
    if (control->minimum >= 0 || control->bits == 0 || control->bits >= 32)
    {
        return (LONG)value;
    }
    ULONG sign = 1ul << (control->bits - 1);
    value &= (sign << 1) - 1;
    return (LONG)(value ^ sign) - (LONG)sign;
}

static uint8_t HatBits(const mwinHidValue* hat, LONG value)
{
    LONG steps = hat->maximum - hat->minimum + 1;
    if (value < hat->minimum || value > hat->maximum || steps <= 0)
    {
        return 0;
    }
    return s_hatBits[(value - hat->minimum) * 8 / steps];
}

// One input report: the buttons down, and each axis's and hat's value.
static void Parse(const mwinWin32Hid* hid, mwinHidPad* pad, BYTE* report, ULONG length)
{
    const mwinHidApi* api = &hid->api;
    mwinPadControls* controls = &pad->controls;
    USAGE down[MWIN_PAD_NUMBERED_BUTTONS];
    ULONG count = MWIN_PAD_NUMBERED_BUTTONS;
    if (api->usages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0, down, &count, pad->preparsed,
                    (PCHAR)report, length) == HIDP_STATUS_SUCCESS)
    {
        memset(controls->buttons, 0, sizeof(controls->buttons));
        for (ULONG i = 0; i < count; i++)
        {
            for (uint8_t b = 0; b < controls->buttonCount; b++)
            {
                controls->buttons[b] = controls->buttons[b] || pad->buttons[b] == down[i];
            }
        }
    }
    for (uint8_t i = 0; i < controls->axisCount + controls->hatCount; i++)
    {
        bool axis = i < controls->axisCount;
        const mwinHidValue* control = axis ? &pad->axes[i] : &pad->hats[i - controls->axisCount];
        ULONG value = 0;
        if (api->usageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, 0, control->usage, &value,
                            pad->preparsed, (PCHAR)report, length) != HIDP_STATUS_SUCCESS)
        {
            continue;
        }
        LONG number = Extend(value, control);
        if (axis)
        {
            controls->axes[i] = mwinPadNormalize(number, control->minimum, control->maximum);
        }
        else
        {
            controls->hats[i - controls->axisCount] = HatBits(control, number);
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
    for (DWORD i = 0; pad != nullptr && i < reports->dwCount; i++)
    {
        size_t start = (size_t)i * reports->dwSizeHid;
        if (offset + start + reports->dwSizeHid > size)
        {
            break;
        }
        Parse(hid, pad, &data.bytes[offset + start], reports->dwSizeHid);
        mwinPostPadControls(hid->context, pad->slot, &pad->controls, timeNs);
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
