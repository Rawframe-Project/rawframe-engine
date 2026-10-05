// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's preferences and facts, and the preferred locales, as the
// backend reports them.

#include "maul-window/system.h"

#include "core.h"

#include "maul-unicode/encoding.h"

#include <string.h>

static void PostGlobalType(mwinContext* context, mwinEventType type, uint64_t timeNs)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    mwinPostGlobal(context, &event);
}

void mwinSetSystemFacts(mwinContext* context, const mwinSystemFacts* facts, uint64_t timeNs)
{
    const mwinSystemFacts* old = &context->facts;
    bool look = old->theme != facts->theme || old->hasAccent != facts->hasAccent ||
                (facts->hasAccent && old->accent != facts->accent) ||
                old->reducedMotion != facts->reducedMotion || old->textScale != facts->textScale;
    bool power = old->onBattery != facts->onBattery || old->lowPower != facts->lowPower;
    context->facts = *facts;
    if (look)
    {
        PostGlobalType(context, mwin_eventThemeChanged, timeNs);
    }
    if (power)
    {
        PostGlobalType(context, mwin_eventPowerChanged, timeNs);
    }
}

bool mwinSetLocales(mwinContext* context, const char* locales, size_t length, uint64_t timeNs)
{
    if (length > context->limits.localeBytes || (locales == nullptr && length != 0) ||
        muniValidateUtf8(locales, length).status != muni_success)
    {
        return false;
    }
    if (length == context->localeLength &&
        (length == 0 || memcmp(context->locales, locales, length) == 0))
    {
        return true;
    }
    if (length > 0)
    {
        memcpy(context->locales, locales, length);
    }
    context->localeLength = (uint16_t)length;
    PostGlobalType(context, mwin_eventLocaleChanged, timeNs);
    return true;
}

mwinResult mwinGetSystemFacts(const mwinContext* context, mwinSystemFacts* factsOut)
{
    if (context == nullptr || factsOut == nullptr)
    {
        return mwinMisuse(context);
    }
    *factsOut = context->facts;
    return mwin_success;
}

mwinResult mwinGetPreferredLocales(const mwinContext* context, char* buffer, size_t capacity,
                                   size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity != 0))
    {
        return mwinMisuse(context);
    }
    size_t length = context->localeLength;
    if (capacity > 0 && length > 0)
    {
        memcpy(buffer, context->locales, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}
