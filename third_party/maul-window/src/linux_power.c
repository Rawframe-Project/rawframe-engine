// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The power facts from the portal and UPower.

#include "linux_power.h"

#include "monotonic.h"

#include <stdio.h>
#include <string.h>

#define PROPERTIES  "org.freedesktop.DBus.Properties"
#define PORTAL      "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define MONITOR     "org.freedesktop.portal.PowerProfileMonitor"
#define SAVER       "power-saver-enabled"
#define UPOWER      "org.freedesktop.UPower"
#define UPOWER_PATH "/org/freedesktop/UPower"
#define BATTERY     "OnBattery"

static bool AppendString(const mwinDBusApi* api, mwinDBusIter* iter, const char* text)
{
    return api->appendBasic(iter, mwin_dbusTypeString, (const void*)&text);
}

// A variant's boolean: -1 when it holds another type.
static int8_t Boolean(const mwinDBusApi* api, mwinDBusIter* variant)
{
    mwinDBusIter value;
    mwinDBusBool boolean = 0;
    api->recurse(variant, &value);
    if (api->argType(&value) != mwin_dbusTypeBoolean)
    {
        return -1;
    }
    api->getBasic(&value, &boolean);
    return boolean != 0 ? 1 : 0;
}

static mwinTristate Tristate(int8_t said)
{
    return said < 0 ? mwin_unknown : said == 1 ? mwin_yes : mwin_no;
}

static void Publish(mwinLinuxPower* power, uint64_t nowNs)
{
    mwinSystemFacts facts = power->context->facts;
    facts.lowPower = Tristate(power->saver);
    facts.onBattery = Tristate(power->battery);
    mwinSetSystemFacts(power->context, &facts, nowNs);
}

// PropertiesChanged(s interface, a{sv} changed, as invalidated): the
// fact of the interface the signal names, where it changed.
static mwinDBusHandled Filter(DBusConnection* connection, DBusMessage* message, void* data)
{
    mwinLinuxPower* power = data;
    const mwinDBusApi* api =
        connection == power->system.connection ? &power->system.api : &power->session->api;
    mwinDBusIter arguments;
    mwinDBusIter changed;
    const char* interface = nullptr;
    if (!api->isSignal(message, PROPERTIES, "PropertiesChanged") ||
        !api->iterInit(message, &arguments) || api->argType(&arguments) != mwin_dbusTypeString)
    {
        return mwin_dbusNotHandled;
    }
    api->getBasic(&arguments, (void*)&interface);
    bool monitor = strcmp(interface, MONITOR) == 0;
    bool upower = strcmp(interface, UPOWER) == 0;
    if (!api->next(&arguments) || api->argType(&arguments) != mwin_dbusTypeArray)
    {
        return mwin_dbusNotHandled;
    }
    api->recurse(&arguments, &changed);
    for (; api->argType(&changed) == mwin_dbusTypeDictEntry; (void)api->next(&changed))
    {
        mwinDBusIter entry;
        const char* name = nullptr;
        api->recurse(&changed, &entry);
        api->getBasic(&entry, (void*)&name);
        (void)api->next(&entry);
        int8_t* fact = monitor && strcmp(name, SAVER) == 0    ? &power->saver
                       : upower && strcmp(name, BATTERY) == 0 ? &power->battery
                                                              : nullptr;
        int8_t said = fact != nullptr ? Boolean(api, &entry) : -1;
        if (said >= 0)
        {
            *fact = said;
        }
    }
    Publish(power, mwinMonotonicNow());
    return mwin_dbusNotHandled;
}

// Listens for an interface's PropertiesChanged on a bus, and asks for
// one of its properties: false when the bus is not there.
static bool Ask(mwinLinuxPower* power, mwinLinuxBus* bus, const char* destination, const char* path,
                const char* interface, const char* name, mwinBusCall* call, uint64_t nowNs)
{
    if (!mwinBusConnect(bus) || !bus->api.addFilter(bus->connection, Filter, power, nullptr))
    {
        return false;
    }
    const mwinDBusApi* api = &bus->api;
    char rule[256];
    (void)snprintf(rule, sizeof(rule),
                   "type='signal',interface='" PROPERTIES "',member='PropertiesChanged',"
                   "path='%s',arg0='%s'",
                   path, interface);
    mwinDBusIter arguments;
    DBusMessage* match = mwinBusMethod(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus", "AddMatch");
    if (match != nullptr)
    {
        api->iterInitAppend(match, &arguments);
        (void)AppendString(api, &arguments, rule);
        mwinBusTell(bus, match);
    }
    DBusMessage* get = mwinBusMethod(bus, destination, path, PROPERTIES, "Get");
    if (get != nullptr)
    {
        api->iterInitAppend(get, &arguments);
        if (AppendString(api, &arguments, interface) && AppendString(api, &arguments, name))
        {
            (void)mwinBusSend(bus, get, call, nowNs);
        }
        else
        {
            api->unrefMessage(get);
        }
    }
    return true;
}

void mwinPowerStart(mwinLinuxPower* power, mwinContext* context, mwinLinuxBus* session,
                    uint64_t nowNs)
{
    *power = (mwinLinuxPower){.context = context,
                              .session = session,
                              .system = {.system = true},
                              .saver = -1,
                              .battery = -1};
    power->sessionListening =
        Ask(power, session, PORTAL, PORTAL_PATH, MONITOR, SAVER, &power->saverCall, nowNs);
    power->systemListening = Ask(power, &power->system, UPOWER, UPOWER_PATH, UPOWER, BATTERY,
                                 &power->batteryCall, nowNs);
}

// Get's answer, a variant with a boolean, once it came; an error's
// answer holds its text instead.
static void Take(mwinLinuxPower* power, mwinLinuxBus* bus, mwinBusCall* call, int8_t* fact,
                 uint64_t nowNs)
{
    bool failed = false;
    DBusMessage* reply =
        call->pending != nullptr ? mwinBusAnswer(bus, call, nowNs, &failed) : nullptr;
    mwinDBusIter arguments;
    if (reply != nullptr && bus->api.iterInit(reply, &arguments) &&
        bus->api.argType(&arguments) == mwin_dbusTypeVariant)
    {
        *fact = Boolean(&bus->api, &arguments);
        Publish(power, nowNs);
    }
    if (reply != nullptr)
    {
        bus->api.unrefMessage(reply);
    }
}

void mwinPowerPump(mwinLinuxPower* power, uint64_t nowNs)
{
    if (power->system.connection != nullptr)
    {
        mwinBusPump(&power->system, 0);
    }
    Take(power, power->session, &power->saverCall, &power->saver, nowNs);
    Take(power, &power->system, &power->batteryCall, &power->battery, nowNs);
}

void mwinPowerStop(mwinLinuxPower* power)
{
    if (power->context == nullptr)
    {
        return;
    }
    mwinBusDrop(power->session, &power->saverCall);
    mwinBusDrop(&power->system, &power->batteryCall);
    if (power->sessionListening)
    {
        power->session->api.removeFilter(power->session->connection, Filter, power);
    }
    if (power->systemListening)
    {
        power->system.api.removeFilter(power->system.connection, Filter, power);
    }
    power->sessionListening = false;
    power->systemListening = false;
    mwinBusClose(&power->system);
}
