// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Settings portal's answers as system facts.

#include "linux_settings.h"

#include "monotonic.h"

#include <string.h>

#define PORTAL      "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define SETTINGS    "org.freedesktop.portal.Settings"
#define APPEARANCE  "org.freedesktop.appearance"
#define GNOME       "org.gnome.desktop.interface"
#define KDE         "org.kde.kdeglobals.KDE"

static bool AppendString(const mwinDBusApi* api, mwinDBusIter* iter, const char* text)
{
    return api->appendBasic(iter, mwin_dbusTypeString, (const void*)&text);
}

static uint8_t Channel(double value)
{
    return (uint8_t)(value * 255.0 + 0.5);
}

// accent-color: three doubles from 0 to 1, out of range for none.
static void TakeAccent(mwinLinuxSettings* settings, mwinDBusIter* value)
{
    const mwinDBusApi* api = &settings->bus->api;
    mwinDBusIter channel;
    api->recurse(value, &channel);
    double rgb[3] = {-1.0, -1.0, -1.0};
    for (int i = 0; i < 3 && api->argType(&channel) == mwin_dbusTypeDouble; i++)
    {
        api->getBasic(&channel, &rgb[i]);
        (void)api->next(&channel);
    }
    bool valid = true;
    for (int i = 0; i < 3; i++)
    {
        valid = valid && rgb[i] >= 0.0 && rgb[i] <= 1.0;
    }
    settings->hasAccent = valid;
    settings->accent = valid ? (uint32_t)Channel(rgb[0]) << 24 | (uint32_t)Channel(rgb[1]) << 16 |
                                   (uint32_t)Channel(rgb[2]) << 8 | 0xFFu
                             : 0;
}

// KDE's animation factor, a number or its text: 0 when it turns
// animations off, 1 when it leaves them on, -1 when it is no number.
static int8_t KdeAnimations(const mwinDBusApi* api, mwinDBusIter* value, int type)
{
    double number = -1.0;
    const char* text = "";
    if (type == mwin_dbusTypeDouble)
    {
        api->getBasic(value, &number);
        return number == 0.0 ? 0 : number > 0.0 ? 1 : -1;
    }
    if (type == mwin_dbusTypeString)
    {
        api->getBasic(value, (void*)&text);
    }
    bool digits = false;
    bool zero = true;
    bool point = false;
    for (; *text != '\0'; text++)
    {
        bool dot = *text == '.' && !point;
        if (!dot && (*text < '0' || *text > '9'))
        {
            return -1;
        }
        point = point || dot;
        digits = digits || !dot;
        zero = zero && (dot || *text == '0');
    }
    return !digits ? -1 : zero ? 0 : 1;
}

// One setting, from the variant that holds it.
static void Take(mwinLinuxSettings* settings, const char* space, const char* key,
                 mwinDBusIter* variant)
{
    const mwinDBusApi* api = &settings->bus->api;
    mwinDBusIter value;
    api->recurse(variant, &value);
    int type = api->argType(&value);
    uint32_t unsigned32 = 0;
    mwinDBusBool boolean = 0;
    if (strcmp(space, APPEARANCE) == 0 && type == mwin_dbusTypeUint32)
    {
        api->getBasic(&value, &unsigned32);
        int8_t said = unsigned32 <= 2 ? (int8_t)unsigned32 : 0;
        settings->colorScheme = strcmp(key, "color-scheme") == 0 ? said : settings->colorScheme;
        settings->reducedMotion =
            strcmp(key, "reduced-motion") == 0 ? (int8_t)(said == 1) : settings->reducedMotion;
    }
    else if (strcmp(space, APPEARANCE) == 0 && strcmp(key, "accent-color") == 0 &&
             type == mwin_dbusTypeStruct)
    {
        TakeAccent(settings, &value);
    }
    else if (strcmp(space, GNOME) == 0 && strcmp(key, "enable-animations") == 0 &&
             type == mwin_dbusTypeBoolean)
    {
        api->getBasic(&value, &boolean);
        settings->animations = boolean != 0 ? 1 : 0;
    }
    else if (strcmp(space, GNOME) == 0 && strcmp(key, "text-scaling-factor") == 0 &&
             type == mwin_dbusTypeDouble)
    {
        api->getBasic(&value, &settings->textScale);
    }
    else if (strcmp(space, KDE) == 0 && strcmp(key, "AnimationDurationFactor") == 0)
    {
        settings->kdeAnimations = KdeAnimations(api, &value, type);
    }
}

// The settings as facts, the power and capabilities left as they were.
static void Publish(mwinLinuxSettings* settings, uint64_t nowNs)
{
    mwinSystemFacts facts = settings->context->facts;
    facts.theme = settings->colorScheme == 1   ? mwin_themeDark
                  : settings->colorScheme == 2 ? mwin_themeLight
                                               : mwin_themeUnknown;
    facts.hasAccent = settings->hasAccent;
    facts.accent = settings->accent;
    facts.reducedMotion = settings->reducedMotion >= 0
                              ? settings->reducedMotion == 1
                              : settings->animations == 0 || settings->kdeAnimations == 0;
    facts.textScale = settings->textScale > 0.0 ? (float)settings->textScale : 1.0f;
    mwinSetSystemFacts(settings->context, &facts, nowNs);
}

// ReadAll's answer: a{sa{sv}}, the keys of each namespace asked for.
static void TakeAll(mwinLinuxSettings* settings, DBusMessage* reply)
{
    const mwinDBusApi* api = &settings->bus->api;
    mwinDBusIter arguments;
    mwinDBusIter spaces;
    if (!api->iterInit(reply, &arguments) || api->argType(&arguments) != mwin_dbusTypeArray)
    {
        return;
    }
    api->recurse(&arguments, &spaces);
    for (; api->argType(&spaces) == mwin_dbusTypeDictEntry; (void)api->next(&spaces))
    {
        mwinDBusIter space;
        mwinDBusIter keys;
        const char* name = nullptr;
        api->recurse(&spaces, &space);
        api->getBasic(&space, (void*)&name);
        (void)api->next(&space);
        api->recurse(&space, &keys);
        for (; api->argType(&keys) == mwin_dbusTypeDictEntry; (void)api->next(&keys))
        {
            mwinDBusIter entry;
            const char* key = nullptr;
            api->recurse(&keys, &entry);
            api->getBasic(&entry, (void*)&key);
            (void)api->next(&entry);
            Take(settings, name, key, &entry);
        }
    }
}

// SettingChanged(s namespace, s key, v value).
static mwinDBusHandled Filter(DBusConnection* connection, DBusMessage* message, void* data)
{
    (void)connection;
    mwinLinuxSettings* settings = data;
    const mwinDBusApi* api = &settings->bus->api;
    mwinDBusIter arguments;
    const char* space = nullptr;
    const char* key = nullptr;
    if (api->isSignal(message, SETTINGS, "SettingChanged") && api->iterInit(message, &arguments) &&
        api->argType(&arguments) == mwin_dbusTypeString)
    {
        api->getBasic(&arguments, (void*)&space);
        if (api->next(&arguments) && api->argType(&arguments) == mwin_dbusTypeString)
        {
            api->getBasic(&arguments, (void*)&key);
            if (api->next(&arguments) && api->argType(&arguments) == mwin_dbusTypeVariant)
            {
                Take(settings, space, key, &arguments);
                Publish(settings, mwinMonotonicNow());
            }
        }
    }
    return mwin_dbusNotHandled;
}

static void Listen(mwinLinuxSettings* settings)
{
    mwinLinuxBus* bus = settings->bus;
    if (!bus->api.addFilter(bus->connection, Filter, settings, nullptr))
    {
        return;
    }
    settings->listening = true;
    DBusMessage* message = mwinBusMethod(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus", "AddMatch");
    mwinDBusIter arguments;
    if (message != nullptr)
    {
        bus->api.iterInitAppend(message, &arguments);
        (void)AppendString(&bus->api, &arguments,
                           "type='signal',interface='" SETTINGS "',member='SettingChanged'");
        mwinBusTell(bus, message);
    }
}

void mwinSettingsStart(mwinLinuxSettings* settings, mwinContext* context, mwinLinuxBus* bus,
                       uint64_t nowNs)
{
    *settings = (mwinLinuxSettings){.context = context,
                                    .bus = bus,
                                    .colorScheme = -1,
                                    .reducedMotion = -1,
                                    .animations = -1,
                                    .kdeAnimations = -1,
                                    .textScale = -1.0};
    if (!mwinBusConnect(bus))
    {
        return;
    }
    Listen(settings);
    DBusMessage* message = mwinBusMethod(bus, PORTAL, PORTAL_PATH, SETTINGS, "ReadAll");
    mwinDBusIter arguments;
    mwinDBusIter spaces;
    if (message == nullptr)
    {
        return;
    }
    const mwinDBusApi* api = &bus->api;
    api->iterInitAppend(message, &arguments);
    bool built = api->openContainer(&arguments, mwin_dbusTypeArray, "s", &spaces) &&
                 AppendString(api, &spaces, APPEARANCE) && AppendString(api, &spaces, GNOME) &&
                 AppendString(api, &spaces, KDE) && api->closeContainer(&arguments, &spaces);
    if (!built)
    {
        api->unrefMessage(message);
        return;
    }
    (void)mwinBusSend(bus, message, &settings->call, nowNs);
}

void mwinSettingsPump(mwinLinuxSettings* settings, uint64_t nowNs)
{
    if (settings->call.pending == nullptr)
    {
        return;
    }
    bool failed = false;
    DBusMessage* reply = mwinBusAnswer(settings->bus, &settings->call, nowNs, &failed);
    if (reply != nullptr && !failed)
    {
        TakeAll(settings, reply);
        Publish(settings, nowNs);
    }
    if (reply != nullptr)
    {
        settings->bus->api.unrefMessage(reply);
    }
}

void mwinSettingsStop(mwinLinuxSettings* settings)
{
    if (settings->bus == nullptr)
    {
        return;
    }
    mwinBusDrop(settings->bus, &settings->call);
    if (settings->listening)
    {
        settings->bus->api.removeFilter(settings->bus->connection, Filter, settings);
        settings->listening = false;
    }
}
