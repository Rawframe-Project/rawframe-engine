// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Linux file dialogs through the desktop portal.

#include "linux_dialog.h"

#include "monotonic.h"
#include "uri_list.h"

#include "maul-window/services.h"

#include <stdio.h>
#include <string.h>

static const char s_portal[] = "org.freedesktop.portal.Desktop";
static const char s_request[] = "org.freedesktop.portal.Request";

void mwinDialogsStart(mwinLinuxDialogs* dialogs, mwinContext* context, mwinLinuxBus* bus)
{
    *dialogs = (mwinLinuxDialogs){.context = context, .bus = bus};
    for (size_t i = 0; i < MWIN_LINUX_DIALOGS; i++)
    {
        dialogs->dialogs[i].zenity.fd = -1;
    }
}

static bool AppendString(const mwinDBusApi* api, mwinDBusIter* iter, const char* text)
{
    return api->appendBasic(iter, mwin_dbusTypeString, (const void*)&text);
}

// An option of a{sv}: its entry and the variant within, to fill in.
static bool OpenOption(const mwinDBusApi* api, mwinDBusIter* options, const char* key,
                       const char* signature, mwinDBusIter entry[2])
{
    return api->openContainer(options, mwin_dbusTypeDictEntry, nullptr, &entry[0]) &&
           AppendString(api, &entry[0], key) &&
           api->openContainer(&entry[0], mwin_dbusTypeVariant, signature, &entry[1]);
}

static bool CloseOption(const mwinDBusApi* api, mwinDBusIter* options, mwinDBusIter entry[2])
{
    return api->closeContainer(&entry[0], &entry[1]) && api->closeContainer(options, &entry[0]);
}

static bool StringOption(const mwinDBusApi* api, mwinDBusIter* options, const char* key,
                         const char* text)
{
    mwinDBusIter entry[2];
    return OpenOption(api, options, key, "s", entry) && AppendString(api, &entry[1], text) &&
           CloseOption(api, options, entry);
}

static bool TrueOption(const mwinDBusApi* api, mwinDBusIter* options, const char* key)
{
    mwinDBusIter entry[2];
    mwinDBusBool yes = 1;
    return OpenOption(api, options, key, "b", entry) &&
           api->appendBasic(&entry[1], mwin_dbusTypeBoolean, (const void*)&yes) &&
           CloseOption(api, options, entry);
}

// A path as the portal takes it: bytes, with their NUL.
static bool BytesOption(const mwinDBusApi* api, mwinDBusIter* options, const char* key,
                        const char* bytes, size_t length)
{
    mwinDBusIter entry[2];
    mwinDBusIter array;
    return OpenOption(api, options, key, "ay", entry) &&
           api->openContainer(&entry[1], mwin_dbusTypeArray, "y", &array) &&
           api->appendFixedArray(&array, mwin_dbusTypeByte, (const void*)&bytes, (int)length) &&
           api->closeContainer(&entry[1], &array) && CloseOption(api, options, entry);
}

// A filter, (sa(us)): its name and a glob (0) an extension.
static bool AppendFilter(const mwinDBusApi* api, mwinDBusIter* iter, const mwinDialogFilter* filter)
{
    mwinDBusIter entry;
    mwinDBusIter patterns;
    bool built = api->openContainer(iter, mwin_dbusTypeStruct, nullptr, &entry) &&
                 AppendString(api, &entry, filter->name) &&
                 api->openContainer(&entry, mwin_dbusTypeArray, "(us)", &patterns);
    const char* extension = filter->extensions;
    while (built && *extension != '\0')
    {
        size_t length = strcspn(extension, ";");
        char glob[5 * MWIN_DIALOG_FILTER_BYTES + 3];
        glob[mwinCaseBlindPattern(extension, length, glob)] = '\0';
        mwinDBusIter pattern;
        uint32_t kind = 0;
        built = api->openContainer(&patterns, mwin_dbusTypeStruct, nullptr, &pattern) &&
                api->appendBasic(&pattern, mwin_dbusTypeUint32, (const void*)&kind) &&
                AppendString(api, &pattern, glob) && api->closeContainer(&patterns, &pattern);
        extension += length + (extension[length] == ';' ? 1 : 0);
    }
    return built && api->closeContainer(&entry, &patterns) && api->closeContainer(iter, &entry);
}

// The filters, the first chosen at first.
static bool FilterOptions(const mwinDBusApi* api, mwinDBusIter* options, const mwinDialogCopy* copy)
{
    mwinDBusIter entry[2];
    mwinDBusIter array;
    bool built = OpenOption(api, options, "filters", "a(sa(us))", entry) &&
                 api->openContainer(&entry[1], mwin_dbusTypeArray, "(sa(us))", &array);
    for (uint32_t i = 0; built && i < copy->filterCount; i++)
    {
        built = AppendFilter(api, &array, &copy->filters[i]);
    }
    return built && api->closeContainer(&entry[1], &array) && CloseOption(api, options, entry) &&
           OpenOption(api, options, "current_filter", "(sa(us))", entry) &&
           AppendFilter(api, &entry[1], &copy->filters[0]) && CloseOption(api, options, entry);
}

static bool Options(const mwinDBusApi* api, mwinDBusIter* arguments, const mwinDialogCopy* copy,
                    const char* token)
{
    mwinDBusIter options;
    bool built = api->openContainer(arguments, mwin_dbusTypeArray, "{sv}", &options) &&
                 StringOption(api, &options, "handle_token", token) &&
                 (copy->kind != mwin_dialogOpenMany || TrueOption(api, &options, "multiple")) &&
                 (copy->kind != mwin_dialogFolder || TrueOption(api, &options, "directory")) &&
                 (copy->filterCount == 0 || FilterOptions(api, &options, copy)) &&
                 (copy->folderLength == 0 || BytesOption(api, &options, "current_folder",
                                                         copy->folder, copy->folderLength + 1)) &&
                 (copy->kind != mwin_dialogSave || copy->nameLength == 0 ||
                  StringOption(api, &options, "current_name", copy->name));
    return built && api->closeContainer(arguments, &options);
}

// The path of the request a token names: the bus name of the
// connection without its ':' and with '_' for '.', then the token.
static void Handle(const mwinLinuxBus* bus, const char* token, char* handle)
{
    char sender[64];
    const char* name = bus->api.uniqueName(bus->connection);
    size_t length = 0;
    for (const char* at = name != nullptr ? name + 1 : ""; *at != '\0' && length < 63; at++)
    {
        sender[length++] = *at == '.' ? '_' : *at;
    }
    sender[length] = '\0';
    (void)snprintf(handle, MWIN_HANDLE_BYTES, "/org/freedesktop/portal/desktop/request/%s/%s",
                   sender, token);
}

// Asks the portal for the dialog: false when the call could not go.
static bool Call(mwinLinuxDialogs* dialogs, mwinLinuxDialog* dialog, const mwinDialogCopy* copy,
                 const char* parent)
{
    mwinLinuxBus* bus = dialogs->bus;
    const mwinDBusApi* api = &bus->api;
    DBusMessage* message = mwinBusMethod(bus, s_portal, "/org/freedesktop/portal/desktop",
                                         "org.freedesktop.portal.FileChooser",
                                         copy->kind == mwin_dialogSave ? "SaveFile" : "OpenFile");
    if (message == nullptr)
    {
        return false;
    }
    char token[32];
    (void)snprintf(token, sizeof(token), "mwin%u", ++dialogs->tokens);
    Handle(bus, token, dialog->handle);
    mwinDBusIter arguments;
    api->iterInitAppend(message, &arguments);
    if (!AppendString(api, &arguments, parent) || !AppendString(api, &arguments, copy->title) ||
        !Options(api, &arguments, copy, token))
    {
        api->unrefMessage(message);
        dialog->handle[0] = '\0';
        return false;
    }
    bool sent = mwinBusSend(bus, message, &dialog->call, mwinMonotonicNow());
    dialog->handle[0] = sent ? dialog->handle[0] : '\0';
    return sent;
}

// Answers a dialog's request with how it ended, the paths gathered when
// done; one whose request went is only let go.
static void Finish(mwinLinuxDialogs* dialogs, mwinLinuxDialog* dialog, mwinOutcome outcome)
{
    mwinContext* context = dialogs->context;
    if (mwinAnswerRequest(context, &dialog->to) != nullptr)
    {
        outcome = mwinSettleDialog(context, dialog->to.slot, dialog->to.request, outcome);
        mwinAnswer(context, &dialog->to, outcome);
    }
    else
    {
        // What was gathered for a request that went goes too.
        mwinBeginDialog(context);
    }
    dialog->to.waiting = false;
    dialog->handle[0] = '\0';
    mwinZenityStop(&dialog->zenity, context);
}

// Gathers the file URIs of a Response's results, a{sv} with "uris".
static void GatherUris(const mwinDBusApi* api, mwinContext* context, mwinDBusIter* results)
{
    mwinDBusIter entry;
    for (bool more = api->argType(results) == mwin_dbusTypeDictEntry; more;
         more = api->next(results))
    {
        const char* key = "";
        mwinDBusIter value;
        mwinDBusIter uris;
        api->recurse(results, &entry);
        api->getBasic(&entry, (void*)&key);
        if (strcmp(key, "uris") != 0 || !api->next(&entry))
        {
            continue;
        }
        api->recurse(&entry, &value);
        if (api->argType(&value) != mwin_dbusTypeArray)
        {
            continue;
        }
        api->recurse(&value, &uris);
        for (bool uri = api->argType(&uris) == mwin_dbusTypeString; uri; uri = api->next(&uris))
        {
            const char* text = "";
            api->getBasic(&uris, (void*)&text);
            char copy[3 * MWIN_ADDRESS_BYTES + 16];
            size_t length = strlen(text);
            char* path = nullptr;
            size_t pathLength = 0;
            if (length < sizeof(copy))
            {
                memcpy(copy, text, length + 1);
                pathLength = mwinFileUriPath(copy, length, &path);
            }
            // A URI of no local file makes the choice fail.
            mwinAddDialogFile(context, path != nullptr ? path : "", pathLength);
        }
    }
}

// A Response, (u a{sv}): 0 chosen, 1 closed, 2 anything else.
static void Respond(mwinLinuxDialogs* dialogs, mwinLinuxDialog* dialog, DBusMessage* message)
{
    const mwinDBusApi* api = &dialogs->bus->api;
    mwinDBusIter arguments;
    mwinDBusIter results;
    uint32_t code = 2;
    if (api->iterInit(message, &arguments) && api->argType(&arguments) == mwin_dbusTypeUint32)
    {
        api->getBasic(&arguments, (void*)&code);
    }
    mwinBeginDialog(dialogs->context);
    bool waiting = mwinAnswerRequest(dialogs->context, &dialog->to) != nullptr;
    if (code == 0 && waiting && api->next(&arguments) &&
        api->argType(&arguments) == mwin_dbusTypeArray)
    {
        api->recurse(&arguments, &results);
        GatherUris(api, dialogs->context, &results);
    }
    Finish(dialogs, dialog,
           code == 0   ? mwin_outcomeDone
           : code == 1 ? mwin_outcomeCancelled
                       : mwin_outcomeFailed);
}

static mwinDBusHandled Filter(DBusConnection* connection, DBusMessage* message, void* data)
{
    (void)connection;
    mwinLinuxDialogs* dialogs = data;
    const mwinDBusApi* api = &dialogs->bus->api;
    const char* path = api->isSignal(message, s_request, "Response") ? api->path(message) : nullptr;
    for (size_t i = 0; path != nullptr && i < MWIN_LINUX_DIALOGS; i++)
    {
        mwinLinuxDialog* dialog = &dialogs->dialogs[i];
        if (dialog->handle[0] != '\0' && strcmp(dialog->handle, path) == 0)
        {
            Respond(dialogs, dialog, message);
            break;
        }
    }
    return mwin_dbusNotHandled;
}

// Has the bus hand this module the portal's Response signals.
static void Listen(mwinLinuxDialogs* dialogs)
{
    mwinLinuxBus* bus = dialogs->bus;
    if (dialogs->listening || !bus->api.addFilter(bus->connection, Filter, dialogs, nullptr))
    {
        return;
    }
    dialogs->listening = true;
    DBusMessage* message = mwinBusMethod(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus", "AddMatch");
    mwinDBusIter arguments;
    if (message != nullptr)
    {
        bus->api.iterInitAppend(message, &arguments);
        (void)AppendString(&bus->api, &arguments,
                           "type='signal',interface='org.freedesktop.portal.Request',"
                           "member='Response'");
        mwinBusTell(bus, message);
    }
}

// Starts zenity in the portal's stead: false when it ended at once.
static bool Fallback(mwinLinuxDialogs* dialogs, mwinLinuxDialog* dialog, const mwinDialogCopy* copy,
                     int* outcome)
{
    *outcome = mwinZenityStart(&dialog->zenity, dialogs->context, copy);
    return *outcome < 0;
}

int mwinDialogsOpen(mwinLinuxDialogs* dialogs, uint32_t slot, uint32_t request, const char* parent)
{
    mwinLinuxDialog* dialog = nullptr;
    for (size_t i = 0; i < MWIN_LINUX_DIALOGS && dialog == nullptr; i++)
    {
        mwinLinuxDialog* candidate = &dialogs->dialogs[i];
        bool free = !candidate->to.waiting && candidate->call.pending == nullptr &&
                    candidate->handle[0] == '\0' && candidate->zenity.pid == 0;
        dialog = free ? candidate : nullptr;
    }
    if (dialog == nullptr)
    {
        return mwin_outcomeFailed;
    }
    const mwinDialogCopy* copy = dialogs->context->windows[slot].requests[request].value.dialog;
    dialog->to = mwinAnswerTo(dialogs->context, slot, request);
    if (mwinBusConnect(dialogs->bus))
    {
        Listen(dialogs);
        if (dialogs->listening && Call(dialogs, dialog, copy, parent))
        {
            return -1;
        }
    }
    int outcome = -1;
    if (!Fallback(dialogs, dialog, copy, &outcome))
    {
        dialog->to.waiting = false;
    }
    return outcome;
}

// Reads the portal's answer to its call: the request's path, or, with
// no portal, zenity instead.
static void PumpCall(mwinLinuxDialogs* dialogs, mwinLinuxDialog* dialog, uint64_t nowNs)
{
    const mwinDBusApi* api = &dialogs->bus->api;
    bool failed = false;
    DBusMessage* reply = mwinBusAnswer(dialogs->bus, &dialog->call, nowNs, &failed);
    mwinDBusIter arguments;
    const char* handle = nullptr;
    if (reply != nullptr && !failed && api->iterInit(reply, &arguments) &&
        api->argType(&arguments) == mwin_dbusTypeObjectPath)
    {
        // A portal before handle tokens names another path.
        api->getBasic(&arguments, (void*)&handle);
        (void)snprintf(dialog->handle, MWIN_HANDLE_BYTES, "%s", handle);
    }
    if (reply != nullptr)
    {
        api->unrefMessage(reply);
    }
    const mwinRequest* request = mwinAnswerRequest(dialogs->context, &dialog->to);
    int outcome = -1;
    if (failed &&
        (request == nullptr || !Fallback(dialogs, dialog, request->value.dialog, &outcome)))
    {
        Finish(dialogs, dialog, outcome >= 0 ? (mwinOutcome)outcome : mwin_outcomeFailed);
    }
    else if (failed)
    {
        dialog->handle[0] = '\0';
    }
}

// Closes a dialog whose request went.
static void Close(mwinLinuxDialogs* dialogs, mwinLinuxDialog* dialog)
{
    mwinLinuxBus* bus = dialogs->bus;
    mwinBusDrop(bus, &dialog->call);
    DBusMessage* message = dialog->handle[0] != '\0'
                               ? mwinBusMethod(bus, s_portal, dialog->handle, s_request, "Close")
                               : nullptr;
    if (message != nullptr)
    {
        mwinBusTell(bus, message);
    }
    Finish(dialogs, dialog, mwin_outcomeCancelled);
}

void mwinDialogsPump(mwinLinuxDialogs* dialogs, uint64_t nowNs)
{
    for (size_t i = 0; i < MWIN_LINUX_DIALOGS; i++)
    {
        mwinLinuxDialog* dialog = &dialogs->dialogs[i];
        if (dialog->call.pending != nullptr)
        {
            PumpCall(dialogs, dialog, nowNs);
        }
        int outcome =
            dialog->zenity.pid != 0 ? mwinZenityPump(&dialog->zenity, dialogs->context) : -1;
        if (outcome >= 0)
        {
            Finish(dialogs, dialog, (mwinOutcome)outcome);
        }
        bool open =
            dialog->call.pending != nullptr || dialog->handle[0] != '\0' || dialog->zenity.pid != 0;
        if (open && mwinAnswerRequest(dialogs->context, &dialog->to) == nullptr)
        {
            Close(dialogs, dialog);
        }
    }
}

void mwinDialogsStop(mwinLinuxDialogs* dialogs)
{
    for (size_t i = 0; i < MWIN_LINUX_DIALOGS; i++)
    {
        mwinLinuxDialog* dialog = &dialogs->dialogs[i];
        dialog->to.waiting = false;
        if (dialog->call.pending != nullptr || dialog->handle[0] != '\0' || dialog->zenity.pid != 0)
        {
            Close(dialogs, dialog);
        }
    }
    if (dialogs->listening)
    {
        dialogs->bus->api.removeFilter(dialogs->bus->connection, Filter, dialogs);
        dialogs->listening = false;
    }
}
