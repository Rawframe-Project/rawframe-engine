// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 monitors.

#include "win32_output.h"

#include "maul-unicode/encoding.h"

#include <shellscalingapi.h>
#include <string.h>

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
    size_t units = wcsnlen(display.DeviceString, sizeof(display.DeviceString) / sizeof(WCHAR));
    size_t needed = 0;
    muniTextResult result =
        muniConvertUtf16ToUtf8((const uint16_t*)display.DeviceString, units, info->name,
                               MWIN_MONITOR_NAME_BYTES, muni_convertReplace, &needed);
    info->nameLength = result.status == muni_success ? (uint32_t)needed : 0;
}

static void ReadInfo(HMONITOR handle, const MONITORINFOEXW* monitor, mwinMonitorInfo* info)
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

static bool SameFacts(const mwinMonitorInfo* a, const mwinMonitorInfo* b)
{
    return memcmp(&a->bounds, &b->bounds, sizeof(a->bounds)) == 0 &&
           memcmp(&a->workArea, &b->workArea, sizeof(a->workArea)) == 0 && a->scale == b->scale &&
           a->refreshMilliHz == b->refreshMilliHz && a->primary == b->primary &&
           a->nameLength == b->nameLength && memcmp(a->name, b->name, a->nameLength) == 0;
}

static BOOL CALLBACK OnMonitor(HMONITOR handle, HDC context, LPRECT rect, LPARAM data)
{
    (void)context;
    (void)rect;
    mwinWin32Platform* platform = mwinWin32Pointer(data);
    MONITORINFOEXW monitor = {0};
    monitor.cbSize = sizeof(monitor);
    int32_t slot = OutputOf(platform, handle);
    if (slot < 0 || !GetMonitorInfoW(handle, (MONITORINFO*)&monitor))
    {
        return TRUE;
    }
    mwinWin32Output* output = &platform->outputs[slot];
    mwinMonitorInfo info = {0};
    ReadInfo(handle, &monitor, &info);
    output->seen = true;
    if (output->monitor < 0)
    {
        output->monitor = mwinAddMonitor(platform->context, &info, mwinWin32Now());
    }
    else if (!SameFacts(&platform->context->monitors[output->monitor].info, &info))
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
    EnumDisplayMonitors(nullptr, nullptr, OnMonitor, (LPARAM)platform);
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
