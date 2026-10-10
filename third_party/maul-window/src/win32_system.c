// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's preferences and facts on Win32.

#include "win32_system.h"

#include "maul-unicode/encoding.h"

#include <dwmapi.h>
#include <string.h>

#define PERSONALIZE   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"
#define DWM           L"Software\\Microsoft\\Windows\\DWM"
#define ACCESSIBILITY L"Software\\Microsoft\\Accessibility"

// DWMWA_USE_IMMERSIVE_DARK_MODE, which older SDKs and mingw lack.
#define DARK_MODE_ATTRIBUTE 20

// A DWORD of the user's registry; false where it is not set.
static bool ReadDword(LPCWSTR key, LPCWSTR name, DWORD* value)
{
    DWORD size = sizeof(*value);
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, value, &size) ==
           ERROR_SUCCESS;
}

static void ReadLook(mwinSystemFacts* facts)
{
    DWORD value = 0;
    facts->theme = !ReadDword(PERSONALIZE, L"AppsUseLightTheme", &value) ? mwin_themeUnknown
                   : value != 0                                          ? mwin_themeLight
                                                                         : mwin_themeDark;
    // The accent as 0xAABBGGRR.
    if (ReadDword(DWM, L"AccentColor", &value))
    {
        facts->hasAccent = true;
        facts->accent = (value & 0xFFu) << 24 | ((value >> 8) & 0xFFu) << 16 |
                        ((value >> 16) & 0xFFu) << 8 | 0xFFu;
    }
    BOOL animations = TRUE;
    facts->reducedMotion =
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations;
    facts->textScale =
        ReadDword(ACCESSIBILITY, L"TextScaleFactor", &value) ? mwinWin32TextScaleOf(value) : 1.0f;
}

float mwinWin32TextScaleOf(DWORD percent)
{
    // Windows' slider runs from 100% to 225%; anything outside 100 to
    // 500 is taken as no setting.
    return percent >= 100 && percent <= 500 ? (float)percent / 100.0f : 1.0f;
}

void mwinWin32PowerFacts(const SYSTEM_POWER_STATUS* power, mwinSystemFacts* facts)
{
    // Flag 128: no battery, so never on one; 255: the flag unknown.
    bool noBattery = power->BatteryFlag != 255 && (power->BatteryFlag & 128) != 0;
    facts->onBattery = noBattery                  ? mwin_no
                       : power->ACLineStatus == 0 ? mwin_yes
                       : power->ACLineStatus == 1 ? mwin_no
                                                  : mwin_unknown;
    // Battery saver.
    facts->lowPower = power->SystemStatusFlag != 0 ? mwin_yes : mwin_no;
}

static void ReadPower(mwinSystemFacts* facts)
{
    SYSTEM_POWER_STATUS power;
    if (GetSystemPowerStatus(&power))
    {
        mwinWin32PowerFacts(&power, facts);
    }
}

uint32_t mwinWin32JoinLocales(WCHAR* units, ULONG count)
{
    // The names end in nulls, the list in two: a comma for each but the
    // last two.
    uint32_t length = count >= 2 ? (uint32_t)count - 2 : 0;
    for (uint32_t i = 0; i < length; i++)
    {
        units[i] = units[i] == 0 ? L',' : units[i];
    }
    return length;
}

// The preferred UI languages as a list with commas, in UTF-8.
static void ReadLocales(mwinWin32Platform* platform)
{
    mwinContext* context = platform->context;
    ULONG languages = 0;
    ULONG units = context->limits.localeBytes + 2u;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languages, platform->localeUnits, &units))
    {
        return;
    }
    uint32_t length = mwinWin32JoinLocales(platform->localeUnits, units);
    size_t bytes = 0;
    muniTextResult converted =
        muniConvertUtf16ToUtf8((const uint16_t*)platform->localeUnits, length, muni_convertReplace,
                               platform->localeText, context->limits.localeBytes, &bytes);
    if (converted.status == muni_success)
    {
        (void)mwinSetLocales(context, platform->localeText, bytes, mwinWin32Now());
    }
}

// Windows 11, whose builds start at 22000, shows snap layouts over a
// maximize button. RtlGetVersion tells the version GetVersionEx hides
// from programs without a manifest naming it.
static bool HasSnapLayouts(void)
{
    LONG(WINAPI * get)(OSVERSIONINFOW*) = nullptr;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr)
    {
        FARPROC found = GetProcAddress(ntdll, "RtlGetVersion");
        memcpy((void*)&get, (const void*)&found, sizeof(get));
    }
    OSVERSIONINFOW version = {.dwOSVersionInfoSize = sizeof(version)};
    return get != nullptr && get(&version) == 0 && version.dwMajorVersion >= 10 &&
           version.dwBuildNumber >= 22000;
}

void mwinWin32ApplyTheme(const mwinWin32Window* window)
{
    BOOL dark = window->platform->context->facts.theme == mwin_themeDark;
    // Windows before 10 20H1 lack the attribute and keep a light frame.
    (void)DwmSetWindowAttribute(window->hwnd, DARK_MODE_ATTRIBUTE, &dark, sizeof(dark));
}

void mwinWin32ReadSystem(mwinWin32Platform* platform)
{
    mwinContext* context = platform->context;
    mwinTheme theme = context->facts.theme;
    mwinSystemFacts facts = {.textScale = 1.0f};
    ReadLook(&facts);
    ReadPower(&facts);
    facts.snapLayouts = HasSnapLayouts();
    mwinSetSystemFacts(context, &facts, mwinWin32Now());
    ReadLocales(platform);
    for (uint32_t i = 0; i < context->limits.windows && facts.theme != theme; i++)
    {
        if (platform->windows[i].hwnd != nullptr)
        {
            mwinWin32ApplyTheme(&platform->windows[i]);
        }
    }
}

bool mwinWin32IsSystemChange(UINT message, WPARAM wParam)
{
    switch (message)
    {
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        return true;
    case WM_POWERBROADCAST:
        return wParam == PBT_APMPOWERSTATUSCHANGE;
    default:
        return false;
    }
}
