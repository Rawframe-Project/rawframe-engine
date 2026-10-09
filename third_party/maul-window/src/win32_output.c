// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 monitors, and their HDR facts: whether HDR output is on and the
// SDR white level from DisplayConfig, the luminances from the EDID
// Windows keeps in the monitor's registry key (mwin-0036), which gives
// the physical size too.

#include "win32_output.h"

#include "edid.h"

#include "maul-unicode/encoding.h"

#include <setupapi.h>
#include <shellscalingapi.h>
#include <string.h>

// The display paths read at once for a refresh, and the modes beside
// them; a desktop with more leaves its HDR facts unknown.
#define MOST_PATHS 32
#define MOST_MODES 64
// The EDID bytes read: a base block and fifteen extensions.
#define MOST_EDID_BYTES 2048
// SDR white comes in thousandths of 80 nits.
#define WHITE_UNIT_NITS 0.08f

// The active display paths, read once for every monitor of a refresh.
typedef struct Scan
{
    mwinWin32Platform* platform;
    DISPLAYCONFIG_PATH_INFO paths[MOST_PATHS];
    UINT32 pathCount;
} Scan;

static mwinPixelRect RectOf(RECT rect)
{
    return (mwinPixelRect){rect.left, rect.top, (uint32_t)(rect.right - rect.left),
                           (uint32_t)(rect.bottom - rect.top)};
}

// The monitor's name: the display device's description, as UTF-8.
static void ReadName(const WCHAR* device, mwinMonitorInfo* info)
{
    DISPLAY_DEVICEW display = {.cb = sizeof(display)};
    if (!EnumDisplayDevicesW(device, 0, &display, 0))
    {
        return;
    }
    // A name too long is cut to the most whole characters that fit.
    size_t units = wcsnlen(display.DeviceString, sizeof(display.DeviceString) / sizeof(WCHAR));
    for (; units > 0; --units)
    {
        size_t needed = 0;
        bool split = IS_HIGH_SURROGATE(display.DeviceString[units - 1]) &&
                     units < sizeof(display.DeviceString) / sizeof(WCHAR) &&
                     IS_LOW_SURROGATE(display.DeviceString[units]);
        if (!split &&
            muniConvertUtf16ToUtf8((const uint16_t*)display.DeviceString, units, info->name,
                                   MWIN_MONITOR_NAME_BYTES, muni_convertReplace, &needed)
                    .status == muni_success)
        {
            info->nameLength = (uint32_t)needed;
            return;
        }
    }
    info->nameLength = 0;
}

// The path whose source is the GDI device (\\.\DISPLAY1), or none.
static const DISPLAYCONFIG_PATH_INFO* PathOf(const Scan* scan, const WCHAR* device)
{
    for (UINT32 i = 0; i < scan->pathCount; i++)
    {
        const DISPLAYCONFIG_PATH_INFO* path = &scan->paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {
            .header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                       .size = sizeof(source),
                       .adapterId = path->sourceInfo.adapterId,
                       .id = path->sourceInfo.id}};
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
            wcsncmp(source.viewGdiDeviceName, device, CCHDEVICENAME) == 0)
        {
            return path;
        }
    }
    return nullptr;
}

// The EDID of the monitor at a device interface path, from its device's
// hardware key (`Device Parameters`): its length, 0 where there is none.
static DWORD ReadEdid(const WCHAR* monitorPath, uint8_t* bytes)
{
    HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    // The detail holds the interface path, which DisplayConfig gives in
    // 128 characters at most.
    union
    {
        SP_DEVICE_INTERFACE_DETAIL_DATA_W data;
        uint8_t bytes[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 128 * sizeof(WCHAR)];
    } detail = {.data = {.cbSize = sizeof(detail.data)}};
    SP_DEVICE_INTERFACE_DATA entry = {.cbSize = sizeof(entry)};
    SP_DEVINFO_DATA device = {.cbSize = sizeof(device)};
    DWORD length = 0;
    if (SetupDiOpenDeviceInterfaceW(set, monitorPath, 0, &entry) &&
        SetupDiGetDeviceInterfaceDetailW(set, &entry, &detail.data, sizeof(detail), nullptr,
                                         &device))
    {
        HKEY key = SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key != INVALID_HANDLE_VALUE)
        {
            DWORD kind = REG_NONE;
            length = MOST_EDID_BYTES;
            if (RegQueryValueExW(key, L"EDID", nullptr, &kind, bytes, &length) != ERROR_SUCCESS ||
                kind != REG_BINARY)
            {
                length = 0;
            }
            (void)RegCloseKey(key);
        }
    }
    (void)SetupDiDestroyDeviceInfoList(set);
    return length;
}

// What the path's target tells: its refresh rate, exact; and from its
// EDID, its image's size and luminances. The HDR facts: output with wide
// color forced on an SDR display (automatic color management) is
// advanced color without HDR; the headroom is the EDID's peak over the
// white level.
static void ReadTarget(const DISPLAYCONFIG_PATH_INFO* path, mwinMonitorInfo* info)
{
    DISPLAYCONFIG_RATIONAL rate = path->targetInfo.refreshRate;
    if (rate.Denominator != 0 && rate.Numerator != 0)
    {
        info->refreshMilliHz =
            (uint32_t)(((uint64_t)rate.Numerator * 1000 + rate.Denominator / 2) / rate.Denominator);
    }
    mwinHdrFacts* hdr = &info->hdr;
    LUID adapter = path->targetInfo.adapterId;
    UINT32 target = path->targetInfo.id;
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color = {
        .header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,
                   .size = sizeof(color),
                   .adapterId = adapter,
                   .id = target}};
    if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS)
    {
        hdr->known = true;
        hdr->active = color.advancedColorEnabled && !color.wideColorEnforced;
    }
    DISPLAYCONFIG_SDR_WHITE_LEVEL white = {
        .header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,
                   .size = sizeof(white),
                   .adapterId = adapter,
                   .id = target}};
    if (hdr->active && DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS)
    {
        hdr->sdrWhiteNits = (float)white.SDRWhiteLevel * WHITE_UNIT_NITS;
    }
    DISPLAYCONFIG_TARGET_DEVICE_NAME name = {
        .header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,
                   .size = sizeof(name),
                   .adapterId = adapter,
                   .id = target}};
    uint8_t edid[MOST_EDID_BYTES];
    DWORD length = DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS
                       ? ReadEdid(name.monitorDevicePath, edid)
                       : 0;
    mwinEdidHdr found;
    if (mwinEdidHdrOf(edid, length, &found))
    {
        hdr->known = true;
        hdr->peakNits = found.peakNits;
        hdr->fullFrameNits = found.frameAverageNits;
    }
    (void)mwinEdidSizeOf(edid, length, &info->widthMm, &info->heightMm);
    if (!hdr->active)
    {
        hdr->headroom = hdr->known ? 1.0f : 0.0f;
    }
    else if (hdr->peakNits > 0.0f && hdr->sdrWhiteNits > 0.0f)
    {
        float headroom = hdr->peakNits / hdr->sdrWhiteNits;
        hdr->headroom = headroom > 1.0f ? headroom : 1.0f;
    }
}

static void ReadInfo(const Scan* scan, HMONITOR handle, const MONITORINFOEXW* monitor,
                     mwinMonitorInfo* info)
{
    info->bounds = RectOf(monitor->rcMonitor);
    info->workArea = RectOf(monitor->rcWork);
    info->primary = (monitor->dwFlags & MONITORINFOF_PRIMARY) != 0;
    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    info->scale = GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY) == S_OK
                      ? mwinWin32Scale(dpiX)
                      : 1.0f;
    DEVMODEW mode = {.dmSize = sizeof(mode)};
    if (EnumDisplaySettingsW(monitor->szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmDisplayFrequency > 1)
    {
        info->refreshMilliHz = mode.dmDisplayFrequency * 1000u;
    }
    ReadName(monitor->szDevice, info);
    const DISPLAYCONFIG_PATH_INFO* path = PathOf(scan, monitor->szDevice);
    if (path != nullptr)
    {
        ReadTarget(path, info);
    }
}

// The output slot of an HMONITOR, taking a vacant one for a new one;
// -1 when every slot is taken.
static int32_t OutputOf(mwinWin32Platform* platform, HMONITOR handle)
{
    int32_t vacant = -1;
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        if (platform->outputs[i].handle == handle)
        {
            return (int32_t)i;
        }
        vacant = vacant < 0 && platform->outputs[i].handle == nullptr ? (int32_t)i : vacant;
    }
    if (vacant >= 0)
    {
        platform->outputs[vacant] = (mwinWin32Output){.handle = handle, .monitor = -1};
    }
    return vacant;
}

static BOOL CALLBACK OnMonitor(HMONITOR handle, HDC context, LPRECT rect, LPARAM data)
{
    (void)context;
    (void)rect;
    const Scan* scan = mwinWin32Pointer(data);
    mwinWin32Platform* platform = scan->platform;
    MONITORINFOEXW monitor = {0};
    monitor.cbSize = sizeof(monitor);
    int32_t slot = OutputOf(platform, handle);
    if (slot < 0 || !GetMonitorInfoW(handle, (MONITORINFO*)&monitor))
    {
        return TRUE;
    }
    mwinWin32Output* output = &platform->outputs[slot];
    mwinMonitorInfo info = {0};
    ReadInfo(scan, handle, &monitor, &info);
    output->seen = true;
    if (output->monitor < 0)
    {
        output->monitor = mwinAddMonitor(platform->context, &info, mwinWin32Now());
    }
    else if (!mwinSameMonitorInfo(&platform->context->monitors[output->monitor].info, &info))
    {
        mwinChangeMonitor(platform->context, (uint32_t)output->monitor, &info, mwinWin32Now());
    }
    return TRUE;
}

void mwinWin32RefreshMonitors(mwinWin32Platform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        platform->outputs[i].seen = false;
    }
    Scan scan = {.platform = platform, .pathCount = MOST_PATHS};
    DISPLAYCONFIG_MODE_INFO modes[MOST_MODES];
    UINT32 modeCount = MOST_MODES;
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &scan.pathCount, scan.paths, &modeCount, modes,
                           nullptr) != ERROR_SUCCESS)
    {
        scan.pathCount = 0;
    }
    EnumDisplayMonitors(nullptr, nullptr, OnMonitor, (LPARAM)&scan);
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        mwinWin32Output* output = &platform->outputs[i];
        if (output->handle != nullptr && !output->seen)
        {
            if (output->monitor >= 0)
            {
                mwinRemoveMonitor(platform->context, (uint32_t)output->monitor, mwinWin32Now());
            }
            *output = (mwinWin32Output){.monitor = -1};
        }
    }
}

int32_t mwinWin32MonitorOf(const mwinWin32Platform* platform, HMONITOR handle)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors && handle != nullptr; i++)
    {
        if (platform->outputs[i].handle == handle)
        {
            return platform->outputs[i].monitor;
        }
    }
    return -1;
}
