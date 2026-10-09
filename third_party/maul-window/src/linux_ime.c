// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods over the session bus.

#include "linux_ime.h"

#include "monotonic.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FCITX         "org.freedesktop.portal.Fcitx"
#define FCITX_PATH    "/org/freedesktop/portal/inputmethod"
#define FCITX_METHOD  "org.fcitx.Fcitx.InputMethod1"
#define FCITX_CONTEXT "org.fcitx.Fcitx.InputContext1"
#define IBUS          "org.freedesktop.portal.IBus"
#define IBUS_PATH     "/org/freedesktop/IBus"
#define IBUS_PORTAL   "org.freedesktop.IBus.Portal"
#define IBUS_CONTEXT  "org.freedesktop.IBus.InputContext"
#define PROGRAM       "maul-window"

// How long a key waits for the input method's answer.
#define KEY_DEADLINE_NS 100000000u

// The bytes of committed text and of a composition kept; more is cut
// at a character's start.
#define COMMIT_BYTES  1024
#define PREEDIT_BYTES 512

enum
{
    frameworkNone = 0,
    frameworkFcitx = 1,
    frameworkIBus = 2,
};

// Fcitx 5's capabilities: a composition the program shows, with formats.
#define FCITX_PREEDIT   ((uint64_t)1 << 1)
#define FCITX_FORMATTED ((uint64_t)1 << 4)
// Its formats: underlined, and the part being converted.
#define FCITX_UNDERLINE 8
#define FCITX_HIGHLIGHT 16
// IBus's capabilities: a composition the program shows, and focus.
#define IBUS_PREEDIT 1u
#define IBUS_FOCUS   8u
// IBus's release bit in a key's state, and its attribute types.
#define IBUS_RELEASE    (1u << 30)
#define IBUS_UNDERLINE  1u
#define IBUS_BACKGROUND 3u

static const char* ContextInterface(const mwinLinuxIme* ime)
{
    return ime->framework == frameworkFcitx ? FCITX_CONTEXT : IBUS_CONTEXT;
}

static const char* Service(const mwinLinuxIme* ime)
{
    return ime->framework == frameworkFcitx ? FCITX : IBUS;
}

// A method call on the input context; NULL without one.
static DBusMessage* ContextMethod(mwinLinuxIme* ime, const char* method)
{
    return ime->ready
               ? mwinBusMethod(ime->bus, Service(ime), ime->path, ContextInterface(ime), method)
               : nullptr;
}

// Tells the input context something whose answer does not matter.
static void Tell(mwinLinuxIme* ime, const char* method)
{
    DBusMessage* message = ContextMethod(ime, method);
    if (message != nullptr)
    {
        mwinBusTell(ime->bus, message);
    }
}

static void TellCaret(mwinLinuxIme* ime)
{
    bool fcitx = ime->framework == frameworkFcitx;
    DBusMessage* message = ContextMethod(ime, fcitx ? "SetCursorRect" : "SetCursorLocation");
    if (message == nullptr)
    {
        return;
    }
    const mwinDBusApi* api = &ime->bus->api;
    int32_t values[4] = {(int32_t)ime->caret.x, (int32_t)ime->caret.y, (int32_t)ime->caret.width,
                         (int32_t)ime->caret.height};
    mwinDBusIter arguments;
    api->iterInitAppend(message, &arguments);
    bool built = true;
    for (int i = 0; i < 4; i++)
    {
        built = built && api->appendBasic(&arguments, mwin_dbusTypeInt32, &values[i]);
    }
    if (built)
    {
        mwinBusTell(ime->bus, message);
    }
    else
    {
        api->unrefMessage(message);
    }
}

// The window's composition ends, when one runs.
static void EndComposition(mwinLinuxIme* ime, int32_t slot)
{
    if (slot < 0 || !ime->context->windows[slot].state.composing)
    {
        return;
    }
    mwinEvent end = {0};
    end.type = mwin_eventImePreedit;
    end.timeNs = mwinMonotonicNow();
    end.data.preedit.caret = -1;
    mwinPost(ime->context, (uint32_t)slot, &end);
}

static void PostText(mwinLinuxIme* ime, const char* bytes, size_t length)
{
    char text[COMMIT_BYTES];
    if (ime->focus < 0 || bytes == nullptr)
    {
        return;
    }
    // Repairing never shortens the text; what passes the bound is cut.
    size_t needed = mwinRepairUtf8(bytes, length, nullptr);
    if (needed > sizeof(text))
    {
        return;
    }
    (void)mwinRepairUtf8(bytes, length, text);
    mwinEvent event = {0};
    event.type = mwin_eventTextInput;
    event.timeNs = mwinMonotonicNow();
    event.data.text = (mwinTextEvent){text, (uint32_t)needed};
    mwinPost(ime->context, (uint32_t)ime->focus, &event);
}

// A composition being built: its text, segments, caret and selection.
typedef struct Preedit
{
    char text[PREEDIT_BYTES];
    uint32_t length;
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
    uint32_t count;
    int32_t caret;
    uint32_t selectionStart;
    uint32_t selectionEnd;
    bool cut;
} Preedit;

// Adds a part of a composition with its style: false when it does not
// fit or is not UTF-8.
static bool AddPart(Preedit* preedit, const char* bytes, mwinPreeditStyle style)
{
    size_t length = bytes != nullptr ? strlen(bytes) : 0;
    if (length > sizeof(preedit->text) - preedit->length ||
        preedit->count == MWIN_MAX_PREEDIT_SEGMENTS ||
        mwinRepairUtf8(bytes, length, nullptr) != length)
    {
        preedit->cut = true;
        return false;
    }
    if (length > 0)
    {
        memcpy(preedit->text + preedit->length, bytes, length);
    }
    preedit->segments[preedit->count++] =
        (mwinPreeditSegment){preedit->length, (uint32_t)length, style};
    preedit->length += (uint32_t)length;
    return true;
}

static void PostPreedit(mwinLinuxIme* ime, const Preedit* preedit)
{
    if (ime->focus < 0)
    {
        return;
    }
    if (preedit->length == 0 || preedit->cut)
    {
        EndComposition(ime, ime->focus);
        return;
    }
    mwinEvent event = {0};
    event.type = mwin_eventImePreedit;
    event.timeNs = mwinMonotonicNow();
    event.data.preedit = (mwinPreeditEvent){
        preedit->text,         preedit->length,   preedit->caret, preedit->selectionStart,
        preedit->selectionEnd, preedit->segments, preedit->count};
    mwinPost(ime->context, (uint32_t)ime->focus, &event);
}

// UpdateFormattedPreedit(a(si) parts, i caret in bytes).
static void TakeFcitxPreedit(mwinLinuxIme* ime, DBusMessage* message)
{
    const mwinDBusApi* api = &ime->bus->api;
    Preedit preedit = {.caret = -1};
    mwinDBusIter arguments;
    mwinDBusIter parts;
    if (!api->iterInit(message, &arguments) || api->argType(&arguments) != mwin_dbusTypeArray)
    {
        return;
    }
    api->recurse(&arguments, &parts);
    for (; api->argType(&parts) == mwin_dbusTypeStruct; (void)api->next(&parts))
    {
        mwinDBusIter part;
        const char* text = nullptr;
        int32_t format = 0;
        api->recurse(&parts, &part);
        if (api->argType(&part) != mwin_dbusTypeString)
        {
            return;
        }
        api->getBasic(&part, (void*)&text);
        if (api->next(&part) && api->argType(&part) == mwin_dbusTypeInt32)
        {
            api->getBasic(&part, &format);
        }
        uint32_t start = preedit.length;
        mwinPreeditStyle style = (format & FCITX_HIGHLIGHT) != 0   ? mwin_preeditTarget
                                 : (format & FCITX_UNDERLINE) != 0 ? mwin_preeditUnderline
                                                                   : mwin_preeditPlain;
        if (AddPart(&preedit, text, style) && style == mwin_preeditTarget &&
            preedit.selectionEnd == 0)
        {
            preedit.selectionStart = start;
            preedit.selectionEnd = preedit.length;
        }
    }
    int32_t caret = -1;
    if (api->next(&arguments) && api->argType(&arguments) == mwin_dbusTypeInt32)
    {
        api->getBasic(&arguments, &caret);
    }
    preedit.caret = caret >= 0 && (uint32_t)caret <= preedit.length ? caret : -1;
    PostPreedit(ime, &preedit);
}

// Enters a variant holding a struct named name, IBus's serialized
// objects: (s name, a{sv} attachments, ...), leaving fields at the
// first field after the attachments; false for another.
static bool EnterObject(const mwinDBusApi* api, mwinDBusIter* iter, const char* name,
                        mwinDBusIter* fields)
{
    mwinDBusIter inside;
    const char* found = nullptr;
    if (api->argType(iter) != mwin_dbusTypeVariant)
    {
        return false;
    }
    api->recurse(iter, &inside);
    if (api->argType(&inside) != mwin_dbusTypeStruct)
    {
        return false;
    }
    api->recurse(&inside, fields);
    if (api->argType(fields) != mwin_dbusTypeString)
    {
        return false;
    }
    api->getBasic(fields, (void*)&found);
    return strcmp(found, name) == 0 && api->next(fields) && api->next(fields);
}

// The byte offset of a character of UTF-8 text, the end past its last.
static uint32_t ByteOf(const char* text, uint32_t length, uint32_t character)
{
    uint32_t at = 0;
    for (uint32_t seen = 0; at < length && seen < character; seen++)
    {
        at++;
        while (at < length && ((unsigned char)text[at] & 0xC0u) == 0x80u)
        {
            at++;
        }
    }
    return at;
}

// An IBusText's string, and the range its background attribute marks
// in characters (its underline is taken as the whole).
static const char* ReadIBusText(const mwinDBusApi* api, mwinDBusIter* iter, uint32_t* startOut,
                                uint32_t* endOut)
{
    mwinDBusIter fields;
    const char* text = nullptr;
    *startOut = 0;
    *endOut = 0;
    if (!EnterObject(api, iter, "IBusText", &fields) ||
        api->argType(&fields) != mwin_dbusTypeString)
    {
        return nullptr;
    }
    api->getBasic(&fields, (void*)&text);
    mwinDBusIter list;
    mwinDBusIter attributes;
    if (!api->next(&fields) || !EnterObject(api, &fields, "IBusAttrList", &list) ||
        api->argType(&list) != mwin_dbusTypeArray)
    {
        return text;
    }
    api->recurse(&list, &attributes);
    for (; api->argType(&attributes) == mwin_dbusTypeVariant; (void)api->next(&attributes))
    {
        mwinDBusIter attribute;
        uint32_t values[4] = {0};
        if (!EnterObject(api, &attributes, "IBusAttribute", &attribute))
        {
            continue;
        }
        for (int i = 0; i < 4 && api->argType(&attribute) == mwin_dbusTypeUint32; i++)
        {
            api->getBasic(&attribute, &values[i]);
            (void)api->next(&attribute);
        }
        if (values[0] == IBUS_BACKGROUND && values[2] < values[3])
        {
            *startOut = values[2];
            *endOut = values[3];
        }
    }
    return text;
}

// UpdatePreeditText(v text, u caret in characters, b visible).
static void TakeIBusPreedit(mwinLinuxIme* ime, DBusMessage* message)
{
    const mwinDBusApi* api = &ime->bus->api;
    mwinDBusIter arguments;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t caret = 0;
    mwinDBusBool visible = 0;
    if (!api->iterInit(message, &arguments))
    {
        return;
    }
    const char* text = ReadIBusText(api, &arguments, &start, &end);
    if (api->next(&arguments) && api->argType(&arguments) == mwin_dbusTypeUint32)
    {
        api->getBasic(&arguments, &caret);
    }
    if (api->next(&arguments) && api->argType(&arguments) == mwin_dbusTypeBoolean)
    {
        api->getBasic(&arguments, &visible);
    }
    Preedit preedit = {.caret = -1};
    size_t length = text != nullptr ? strlen(text) : 0;
    if (visible && length > 0 && length <= sizeof(preedit.text) &&
        mwinRepairUtf8(text, length, nullptr) == length)
    {
        // The marked part is the one being converted, the rest
        // underlined.
        uint32_t from = ByteOf(text, (uint32_t)length, start);
        uint32_t to = ByteOf(text, (uint32_t)length, end);
        memcpy(preedit.text, text, length);
        preedit.length = (uint32_t)length;
        uint32_t bounds[4] = {0, from, to, (uint32_t)length};
        for (int i = 0; i < 3; i++)
        {
            if (bounds[i + 1] > bounds[i])
            {
                preedit.segments[preedit.count++] =
                    (mwinPreeditSegment){bounds[i], bounds[i + 1] - bounds[i],
                                         i == 1 ? mwin_preeditTarget : mwin_preeditUnderline};
            }
        }
        preedit.selectionStart = from < to ? from : 0;
        preedit.selectionEnd = from < to ? to : 0;
        preedit.caret = (int32_t)ByteOf(text, (uint32_t)length, caret);
    }
    PostPreedit(ime, &preedit);
}

// The input context's signals: committed text and compositions.
static mwinDBusHandled Filter(DBusConnection* connection, DBusMessage* message, void* data)
{
    (void)connection;
    mwinLinuxIme* ime = data;
    const mwinDBusApi* api = &ime->bus->api;
    const char* path = api->path(message);
    if (!ime->ready || path == nullptr || strcmp(path, ime->path) != 0)
    {
        return mwin_dbusNotHandled;
    }
    mwinDBusIter arguments;
    const char* text = nullptr;
    uint32_t start = 0;
    uint32_t end = 0;
    if (api->isSignal(message, FCITX_CONTEXT, "CommitString") &&
        api->iterInit(message, &arguments) && api->argType(&arguments) == mwin_dbusTypeString)
    {
        api->getBasic(&arguments, (void*)&text);
        PostText(ime, text, strlen(text));
    }
    else if (api->isSignal(message, FCITX_CONTEXT, "UpdateFormattedPreedit"))
    {
        TakeFcitxPreedit(ime, message);
    }
    else if (api->isSignal(message, IBUS_CONTEXT, "CommitText") &&
             api->iterInit(message, &arguments))
    {
        text = ReadIBusText(api, &arguments, &start, &end);
        if (text != nullptr)
        {
            PostText(ime, text, strlen(text));
        }
    }
    else if (api->isSignal(message, IBUS_CONTEXT, "UpdatePreeditText"))
    {
        TakeIBusPreedit(ime, message);
    }
    else if (api->isSignal(message, IBUS_CONTEXT, "HidePreeditText"))
    {
        EndComposition(ime, ime->focus);
    }
    return mwin_dbusNotHandled;
}

// Listens for the context's signals, and asks for compositions the
// program shows.
static void Listen(mwinLinuxIme* ime)
{
    mwinLinuxBus* bus = ime->bus;
    const mwinDBusApi* api = &bus->api;
    ime->listening = api->addFilter(bus->connection, Filter, ime, nullptr);
    char rule[MWIN_IME_PATH_BYTES + 96];
    DBusMessage* match = mwinBusMethod(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus", "AddMatch");
    mwinDBusIter arguments;
    if (match != nullptr)
    {
        const char* text = rule;
        (void)snprintf(rule, sizeof(rule), "type='signal',interface='%s',path='%s'",
                       ContextInterface(ime), ime->path);
        api->iterInitAppend(match, &arguments);
        (void)api->appendBasic(&arguments, mwin_dbusTypeString, (const void*)&text);
        mwinBusTell(bus, match);
    }
    bool fcitx = ime->framework == frameworkFcitx;
    DBusMessage* message = ContextMethod(ime, fcitx ? "SetCapability" : "SetCapabilities");
    if (message != nullptr)
    {
        uint64_t fcitxCapabilities = FCITX_PREEDIT | FCITX_FORMATTED;
        uint32_t ibusCapabilities = IBUS_PREEDIT | IBUS_FOCUS;
        api->iterInitAppend(message, &arguments);
        (void)(fcitx ? api->appendBasic(&arguments, mwin_dbusTypeUint64, &fcitxCapabilities)
                     : api->appendBasic(&arguments, mwin_dbusTypeUint32, &ibusCapabilities));
        mwinBusTell(bus, message);
    }
}

// Asks the next framework not tried for an input context: false when
// every one was.
static bool Create(mwinLinuxIme* ime, uint64_t nowNs)
{
    const char* modifiers = getenv("XMODIFIERS");
    bool fcitxFirst = modifiers != nullptr && strstr(modifiers, "@im=fcitx") != nullptr;
    uint8_t order[2] = {fcitxFirst ? frameworkFcitx : frameworkIBus,
                        fcitxFirst ? frameworkIBus : frameworkFcitx};
    for (int i = 0; i < 2; i++)
    {
        uint8_t framework = order[i];
        if ((ime->tried & framework) != 0)
        {
            continue;
        }
        ime->tried |= framework;
        ime->framework = framework;
        bool fcitx = framework == frameworkFcitx;
        DBusMessage* message =
            mwinBusMethod(ime->bus, fcitx ? FCITX : IBUS, fcitx ? FCITX_PATH : IBUS_PATH,
                          fcitx ? FCITX_METHOD : IBUS_PORTAL, "CreateInputContext");
        if (message == nullptr)
        {
            continue;
        }
        const mwinDBusApi* api = &ime->bus->api;
        const char* program = PROGRAM;
        const char* key = "program";
        mwinDBusIter arguments;
        mwinDBusIter list;
        mwinDBusIter pair;
        api->iterInitAppend(message, &arguments);
        bool built =
            fcitx ? api->openContainer(&arguments, mwin_dbusTypeArray, "(ss)", &list) &&
                        api->openContainer(&list, mwin_dbusTypeStruct, nullptr, &pair) &&
                        api->appendBasic(&pair, mwin_dbusTypeString, (const void*)&key) &&
                        api->appendBasic(&pair, mwin_dbusTypeString, (const void*)&program) &&
                        api->closeContainer(&list, &pair) && api->closeContainer(&arguments, &list)
                  : api->appendBasic(&arguments, mwin_dbusTypeString, (const void*)&program);
        if (built && mwinBusSend(ime->bus, message, &ime->create, nowNs))
        {
            return true;
        }
        if (!built)
        {
            api->unrefMessage(message);
        }
    }
    ime->framework = frameworkNone;
    return false;
}

// Tells the context which window it follows, once it is made.
static void Apply(mwinLinuxIme* ime)
{
    if (ime->focus >= 0)
    {
        Tell(ime, "FocusIn");
        TellCaret(ime);
    }
    else
    {
        Tell(ime, "Reset");
        Tell(ime, "FocusOut");
    }
}

// Takes the answer to CreateInputContext: the context's path, or a try
// of the next framework.
static void PumpCreate(mwinLinuxIme* ime, uint64_t nowNs)
{
    bool failed = false;
    DBusMessage* reply = mwinBusAnswer(ime->bus, &ime->create, nowNs, &failed);
    if (reply == nullptr && !failed)
    {
        return;
    }
    const mwinDBusApi* api = &ime->bus->api;
    mwinDBusIter arguments;
    const char* path = nullptr;
    if (reply != nullptr && !failed && api->iterInit(reply, &arguments) &&
        api->argType(&arguments) == mwin_dbusTypeObjectPath)
    {
        api->getBasic(&arguments, (void*)&path);
    }
    if (path != nullptr && strlen(path) < sizeof(ime->path))
    {
        memcpy(ime->path, path, strlen(path) + 1);
        ime->ready = true;
        Listen(ime);
        Apply(ime);
    }
    if (reply != nullptr)
    {
        api->unrefMessage(reply);
    }
    if (!ime->ready)
    {
        (void)Create(ime, nowNs);
    }
}

void mwinImeStart(mwinLinuxIme* ime, mwinContext* context, mwinLinuxBus* bus)
{
    *ime = (mwinLinuxIme){.context = context, .bus = bus, .focus = -1};
}

void mwinImeFocus(mwinLinuxIme* ime, int32_t slot, mwinRect caret)
{
    bool moved = slot != ime->focus;
    bool caretMoved = caret.x != ime->caret.x || caret.y != ime->caret.y ||
                      caret.width != ime->caret.width || caret.height != ime->caret.height;
    if (moved)
    {
        EndComposition(ime, ime->focus);
    }
    ime->focus = slot;
    ime->caret = caret;
    if (slot >= 0 && ime->tried == 0 && ime->create.pending == nullptr && mwinBusConnect(ime->bus))
    {
        (void)Create(ime, mwinMonotonicNow());
    }
    if (moved)
    {
        Apply(ime);
    }
    else if (caretMoved && slot >= 0)
    {
        TellCaret(ime);
    }
}

// Posts a held key as the backend made it, when its window lives.
static void Deliver(mwinLinuxIme* ime, const mwinImeKey* held)
{
    mwinPost(ime->context, held->slot, &held->key);
    if (held->length > 0)
    {
        mwinEvent typed = {0};
        typed.type = mwin_eventTextInput;
        typed.timeNs = held->key.timeNs;
        typed.data.text = (mwinTextEvent){held->text, held->length};
        mwinPost(ime->context, held->slot, &typed);
    }
}

// Lets the oldest held key go: posted unless the method took it, or
// took the press it releases.
static void Release(mwinLinuxIme* ime, bool taken)
{
    mwinImeKey* held = &ime->held[ime->first];
    uint8_t bit = (uint8_t)(1u << (held->keycode & 7u));
    uint8_t* pressed = &ime->taken[held->keycode >> 3];
    mwinBusDrop(ime->bus, &held->call);
    if (held->key.type == mwin_eventKeyUp)
    {
        taken = taken || (*pressed & bit) != 0;
        *pressed = (uint8_t)(*pressed & ~bit);
    }
    else
    {
        *pressed = taken ? (uint8_t)(*pressed | bit) : (uint8_t)(*pressed & ~bit);
    }
    if (!taken)
    {
        Deliver(ime, held);
    }
    ime->first = (ime->first + 1) % MWIN_IME_HELD;
    ime->count -= 1;
}

// Asks the input method about a key without waiting.
static void Ask(mwinLinuxIme* ime, mwinImeKey* held, uint32_t keysym, uint32_t keycode,
                uint32_t state, uint64_t nowNs)
{
    bool fcitx = ime->framework == frameworkFcitx;
    bool release = held->key.type == mwin_eventKeyUp;
    DBusMessage* message = ContextMethod(ime, "ProcessKeyEvent");
    if (message == nullptr)
    {
        return;
    }
    const mwinDBusApi* api = &ime->bus->api;
    mwinDBusIter arguments;
    uint32_t code = fcitx ? keycode : keycode - 8u;
    uint32_t modifiers = fcitx || !release ? state : state | IBUS_RELEASE;
    mwinDBusBool released = release;
    uint32_t time = 0;
    api->iterInitAppend(message, &arguments);
    bool built = api->appendBasic(&arguments, mwin_dbusTypeUint32, &keysym) &&
                 api->appendBasic(&arguments, mwin_dbusTypeUint32, &code) &&
                 api->appendBasic(&arguments, mwin_dbusTypeUint32, &modifiers) &&
                 (!fcitx || (api->appendBasic(&arguments, mwin_dbusTypeBoolean, &released) &&
                             api->appendBasic(&arguments, mwin_dbusTypeUint32, &time)));
    if (!built)
    {
        api->unrefMessage(message);
        return;
    }
    if (mwinBusSend(ime->bus, message, &held->call, nowNs))
    {
        held->call.deadlineNs = nowNs + KEY_DEADLINE_NS;
    }
}

bool mwinImeOffer(mwinLinuxIme* ime, uint32_t slot, uint32_t keysym, uint32_t keycode,
                  uint32_t state, const mwinEvent* key, const char* text, uint32_t length,
                  uint64_t nowNs)
{
    bool ask = ime->ready && ime->focus == (int32_t)slot && keycode >= 8;
    if ((!ask && ime->count == 0) || length > MWIN_IME_TEXT_BYTES)
    {
        return false;
    }
    if (ime->count == MWIN_IME_HELD)
    {
        Release(ime, false);
    }
    mwinImeKey* held = &ime->held[(ime->first + ime->count) % MWIN_IME_HELD];
    *held = (mwinImeKey){.slot = slot, .keycode = (uint8_t)keycode, .key = *key, .length = length};
    if (length > 0)
    {
        memcpy(held->text, text, length);
    }
    ime->count += 1;
    if (ask)
    {
        Ask(ime, held, keysym, keycode, state, nowNs);
    }
    return true;
}

void mwinImePump(mwinLinuxIme* ime, uint64_t nowNs)
{
    if (ime->create.pending != nullptr)
    {
        PumpCreate(ime, nowNs);
    }
    const mwinDBusApi* api = &ime->bus->api;
    while (ime->count > 0)
    {
        mwinImeKey* held = &ime->held[ime->first];
        bool taken = false;
        if (held->call.pending != nullptr)
        {
            bool failed = false;
            DBusMessage* reply = mwinBusAnswer(ime->bus, &held->call, nowNs, &failed);
            if (reply == nullptr && !failed)
            {
                return;
            }
            mwinDBusIter arguments;
            mwinDBusBool answer = 0;
            if (reply != nullptr && !failed && api->iterInit(reply, &arguments) &&
                api->argType(&arguments) == mwin_dbusTypeBoolean)
            {
                api->getBasic(&arguments, &answer);
            }
            taken = answer != 0;
            if (reply != nullptr)
            {
                api->unrefMessage(reply);
            }
        }
        Release(ime, taken);
    }
}

void mwinImeStop(mwinLinuxIme* ime)
{
    if (ime->bus == nullptr)
    {
        return;
    }
    while (ime->count > 0)
    {
        Release(ime, false);
    }
    mwinBusDrop(ime->bus, &ime->create);
    if (ime->ready && ime->framework == frameworkFcitx)
    {
        Tell(ime, "DestroyIC");
    }
    else if (ime->ready)
    {
        Tell(ime, "Destroy");
    }
    if (ime->listening)
    {
        ime->bus->api.removeFilter(ime->bus->connection, Filter, ime);
        ime->listening = false;
    }
    ime->ready = false;
}
