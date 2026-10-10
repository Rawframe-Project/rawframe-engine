// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter's events (record mui-0008): signals of
// org.a11y.atspi.Event.Object for what an update changed. States and
// properties come from comparing each replaced record with its new one;
// the structure from a walk of the shown tree, compared with what
// clients were last told; the focus last, so a node is added before it
// is focused.

#include "access_record.h"
#include "access_text.h"
#include "allocator.h"
#include "atspi.h"

#include <string.h>

#define INTERFACE_EVENT  "org.a11y.atspi.Event.Object"
#define INTERFACE_WINDOW "org.a11y.atspi.Event.Window"

// What a signal carries in its variant: an integer, a string, a number,
// a role or an object.
typedef struct Any
{
    int type;
    const void* value;
    const muiAtspiObject* object;
} Any;

// Sends a signal (siiva{sv}) of an interface from an object; with no
// client, the bus drops it.
static void EmitOf(muiAtspiApp* app, const muiAtspiObject* from, const char* interface,
                   const char* member, const char* kind, const int32_t details[2], const Any* any)
{
    const muiDBusApi* dbus = &app->dbus;
    char path[ATSPI_PATH_SIZE];
    muiAtspiPathOf(from, path);
    DBusMessage* signal = dbus->newSignal(path, interface, member);
    if (signal == nullptr)
    {
        return;
    }
    const int32_t none = 0;
    muiDBusIter iter;
    muiDBusIter variant;
    muiDBusIter properties;
    dbus->iterInitAppend(signal, &iter);
    bool ok = muiAtspiAppendString(app, &iter, kind) &&
              dbus->appendBasic(&iter, mui_dbusTypeInt32, &details[0]) &&
              dbus->appendBasic(&iter, mui_dbusTypeInt32, &details[1]);
    if (ok && any != nullptr && any->object != nullptr)
    {
        ok = dbus->openContainer(&iter, mui_dbusTypeVariant, "(so)", &variant) &&
             muiAtspiAppendReference(app, &variant, any->object) &&
             dbus->closeContainer(&iter, &variant);
    }
    else if (ok)
    {
        ok = any != nullptr ? muiAtspiAppendVariant(app, &iter, any->type, any->value)
                            : muiAtspiAppendVariant(app, &iter, mui_dbusTypeInt32, &none);
    }
    ok = ok && dbus->openContainer(&iter, mui_dbusTypeArray, "{sv}", &properties) &&
         dbus->closeContainer(&iter, &properties);
    if (ok)
    {
        (void)dbus->send(app->connection, signal, nullptr);
    }
    dbus->unrefMessage(signal);
}

static void Emit(muiAtspiApp* app, const muiAtspiObject* from, const char* member, const char* kind,
                 int32_t detail1, const Any* any)
{
    const int32_t details[2] = {detail1, 0};
    EmitOf(app, from, INTERFACE_EVENT, member, kind, details, any);
}

static void EmitState(muiAtspiApp* app, const muiAtspiObject* from, const char* state, bool on)
{
    Emit(app, from, "StateChanged", state, on ? 1 : 0, nullptr);
}

static void TellStates(muiAtspiApp* app, const muiAtspiObject* object, const muiAccessNode* old)
{
    uint32_t before[2];
    uint32_t after[2];
    muiAtspiRecordStatesOf(old, before);
    muiAtspiRecordStatesOf(object->node, after);
    for (uint32_t state = 0; state <= MUI_ATSPI_LAST_STATE; state++)
    {
        uint32_t bit = 1u << (state % 32);
        uint32_t was = before[state / 32] & bit;
        uint32_t is = after[state / 32] & bit;
        if (was != is)
        {
            EmitState(app, object, muiAtspiStateName(state), is != 0);
        }
    }
}

// The name changed: the node's whole name, as clients read it, and
// spoken when the node is live.
static void TellName(muiAtspiApp* app, const muiAtspiObject* object)
{
    const muiAccessTree* tree = object->adapter->tree;
    uint64_t id = object->node->id;
    size_t length = 0;
    char small[256];
    char* name = small;
    muiResult status = muiAccessTree_GetName(tree, id, small, sizeof(small), &length);
    if (status == mui_errorCapacity)
    {
        name = muiAllocate(&app->allocator, length + 1, 1);
        status = name != nullptr ? muiAccessTree_GetName(tree, id, name, length + 1, &length)
                                 : mui_errorCapacity;
    }
    // A name gone is told as empty; nothing is announced.
    if (status == mui_empty)
    {
        const char* none = "";
        const Any empty = {mui_dbusTypeString, (const void*)&none, nullptr};
        Emit(app, object, "PropertyChange", "accessible-name", 0, &empty);
    }
    if (status == mui_success)
    {
        const Any text = {mui_dbusTypeString, (const void*)&name, nullptr};
        Emit(app, object, "PropertyChange", "accessible-name", 0, &text);
        muiAccessLive live = object->node->values.live;
        if (live != mui_liveOff)
        {
            // AT-SPI's politeness is Maul UI's: polite 1, assertive 2.
            Emit(app, object, "Announcement", "", (int32_t)live, &text);
        }
    }
    if (name != small && name != nullptr)
    {
        muiRelease(&app->allocator, name, length + 1, 1);
    }
}

static void TellProperties(muiAtspiApp* app, const muiAtspiObject* object, const muiAccessNode* old)
{
    const muiAccessNode* now = object->node;
    if (muiRecordNameDiffers(old, now))
    {
        TellName(app, object);
    }
    if (muiRecordTextDiffers(old, now, mui_accessDescription))
    {
        const char* text =
            now->text[mui_accessDescription] != nullptr ? now->text[mui_accessDescription] : "";
        const Any any = {mui_dbusTypeString, (const void*)&text, nullptr};
        Emit(app, object, "PropertyChange", "accessible-description", 0, &any);
    }
    if ((now->flags & mui_accessNumeric) != 0 && now->value != old->value)
    {
        const double value = (double)now->value;
        const Any any = {mui_dbusTypeDouble, &value, nullptr};
        Emit(app, object, "PropertyChange", "accessible-value", 0, &any);
    }
    if (now->role != old->role)
    {
        const uint32_t role = muiAtspiRoleOf(object->adapter->tree, now);
        const Any any = {mui_dbusTypeUint32, &role, nullptr};
        Emit(app, object, "PropertyChange", "accessible-role", 0, &any);
    }
}

static bool IsLead(char byte)
{
    return ((unsigned char)byte & 0xC0u) != 0x80u;
}

// Tells text deleted or inserted: bytes from start up to end of a text,
// with their offset and length in characters.
static void TellEdit(muiAtspiApp* app, const muiAtspiObject* object, const char* kind,
                     const char* text, uint32_t start, uint32_t end)
{
    char* copy = muiAllocate(&app->allocator, (size_t)(end - start) + 1, 1);
    if (copy == nullptr)
    {
        return;
    }
    memcpy(copy, text + start, end - start);
    copy[end - start] = '\0';
    const int32_t first = (int32_t)muiAccessPointsBefore(text, start);
    const int32_t details[2] = {first, (int32_t)muiAccessPointsBefore(text, end) - first};
    const Any any = {mui_dbusTypeString, (const void*)&copy, nullptr};
    EmitOf(app, object, INTERFACE_EVENT, "TextChanged", kind, details, &any);
    muiRelease(&app->allocator, copy, (size_t)(end - start) + 1, 1);
}

// A value text changed: what was deleted, then what was inserted, the
// start and end they share kept, as Chromium tells an edit; whether it
// did.
static bool TellEdits(muiAtspiApp* app, const muiAtspiObject* object, const muiAccessNode* old)
{
    const muiAccessNode* now = object->node;
    const char* a = old->text[mui_accessValue] != nullptr ? old->text[mui_accessValue] : "";
    const char* b = now->text[mui_accessValue] != nullptr ? now->text[mui_accessValue] : "";
    uint32_t aLength = old->text[mui_accessValue] != nullptr ? old->textLength[mui_accessValue] : 0;
    uint32_t bLength = now->text[mui_accessValue] != nullptr ? now->textLength[mui_accessValue] : 0;
    if (aLength == bLength && memcmp(a, b, aLength) == 0)
    {
        return false;
    }
    uint32_t start = 0;
    while (start < aLength && start < bLength && a[start] == b[start])
    {
        start++;
    }
    // Back to a character's start in both.
    while (start > 0 &&
           ((start < aLength && !IsLead(a[start])) || (start < bLength && !IsLead(b[start]))))
    {
        start--;
    }
    uint32_t aEnd = aLength;
    uint32_t bEnd = bLength;
    while (aEnd > start && bEnd > start && a[aEnd - 1] == b[bEnd - 1])
    {
        aEnd--;
        bEnd--;
    }
    while (aEnd < aLength && !IsLead(a[aEnd]))
    {
        aEnd++;
        bEnd++;
    }
    if (aEnd > start)
    {
        TellEdit(app, object, "delete", a, start, aEnd);
    }
    if (bEnd > start)
    {
        TellEdit(app, object, "insert", b, start, bEnd);
    }
    return true;
}

// The selection's ends in characters, the lower first; -1 for a caret
// alone or none.
static void SelectionOf(const muiAccessNode* node, int32_t endsOut[2])
{
    const muiAccessTextMarks* marks = &node->marks;
    const char* text = node->text[mui_accessValue];
    endsOut[0] = -1;
    endsOut[1] = -1;
    if (text != nullptr && marks->selected && marks->anchor != marks->focus)
    {
        int32_t anchor = (int32_t)muiAccessPointsBefore(text, marks->anchor);
        int32_t focus = (int32_t)muiAccessPointsBefore(text, marks->focus);
        endsOut[0] = anchor < focus ? anchor : focus;
        endsOut[1] = anchor < focus ? focus : anchor;
    }
}

// Tells a text object's edits, then where its caret went, then whether
// its selection changed, the order Orca reads them in.
static void TellText(muiAtspiApp* app, const muiAtspiObject* object, const muiAccessNode* old)
{
    const muiAccessNode* now = object->node;
    if (!muiAtspiHasText(now))
    {
        return;
    }
    // A label's new text is told by its name; edits are told of what is
    // typed into, so a list that reuses its rows sends no flood.
    bool edited = muiAccessIsEdited(now) && TellEdits(app, object, old);
    const char* oldText = old->text[mui_accessValue] != nullptr ? old->text[mui_accessValue] : "";
    const char* text = now->text[mui_accessValue] != nullptr ? now->text[mui_accessValue] : "";
    int32_t was =
        old->marks.selected ? (int32_t)muiAccessPointsBefore(oldText, old->marks.focus) : -1;
    int32_t is = now->marks.selected ? (int32_t)muiAccessPointsBefore(text, now->marks.focus) : -1;
    if (is >= 0 && (is != was || edited))
    {
        Emit(app, object, "TextCaretMoved", "", is, nullptr);
    }
    int32_t before[2];
    int32_t after[2];
    SelectionOf(old, before);
    SelectionOf(now, after);
    if (before[0] != after[0] || before[1] != after[1])
    {
        Emit(app, object, "TextSelectionChanged", "", 0, nullptr);
    }
}

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    muiAtspiAdapter* adapter = user;
    const muiAtspiObject object = {adapter, muiAccessTree_Find(tree, old->id)};
    TellStates(adapter->app, &object, old);
    TellProperties(adapter->app, &object, old);
    TellText(adapter->app, &object, old);
}

static void ShownChanged(void* user, const muiAccessTree* tree)
{
    (void)tree;
    ((muiAtspiAdapter*)user)->reshaped = true;
}

// The window root's index among the application root's children.
static int32_t WindowIndex(const muiAtspiAdapter* adapter)
{
    const muiAtspiApp* app = adapter->app;
    int32_t index = 0;
    for (uint32_t i = 0; i < app->windowCount && app->windows[i] != adapter; i++)
    {
        index += muiAccessTree_GetRoot(app->windows[i]->tree) != 0 ? 1 : 0;
    }
    return index;
}

// A told record's object: a node of the window, or the application's
// root for 0. A node gone from the tree keeps its id for its path.
static muiAtspiObject ObjectOf(muiAtspiAdapter* adapter, uint64_t id, muiAccessNode* stand)
{
    if (id == 0)
    {
        return (muiAtspiObject){0};
    }
    const muiAccessNode* node = muiAccessTree_Find(adapter->tree, id);
    if (node == nullptr)
    {
        *stand = (muiAccessNode){.id = id};
        node = stand;
    }
    return (muiAtspiObject){adapter, node};
}

// Tells that a child was added under a parent clients know, or removed
// (index -1).
static void TellChild(muiAtspiAdapter* adapter, uint64_t parent, uint64_t child, int32_t index)
{
    muiAccessNode parentStand;
    muiAccessNode childStand;
    const muiAtspiObject from = ObjectOf(adapter, parent, &parentStand);
    const muiAtspiObject object = ObjectOf(adapter, child, &childStand);
    const Any any = {0, nullptr, &object};
    Emit(adapter->app, &from, "ChildrenChanged", index >= 0 ? "add" : "remove", index, &any);
}

// Walks the shown tree in breadth, from the root: every shown node in
// walk, with where it was found. How many.
static uint32_t Walk(muiAtspiAdapter* adapter)
{
    uint64_t root = muiAccessTree_GetRoot(adapter->tree);
    if (root == 0)
    {
        return 0;
    }
    uint32_t count = 0;
    adapter->walk[count] = root;
    adapter->places[count++] = (muiAtspiPlace){UINT32_MAX, (uint32_t)WindowIndex(adapter), false};
    for (uint32_t at = 0; at < count; at++)
    {
        uint32_t children = 0;
        if (muiAccessTree_GetShownChildren(adapter->tree, adapter->walk[at], adapter->scratch,
                                           adapter->nodes, &children) != mui_success)
        {
            children = 0;
        }
        for (uint32_t i = 0; i < children && count < adapter->nodes; i++)
        {
            adapter->walk[count] = adapter->scratch[i];
            adapter->places[count++] = (muiAtspiPlace){at, i, false};
        }
    }
    return count;
}

static uint64_t ParentAt(const muiAtspiAdapter* adapter, uint32_t at)
{
    uint32_t parent = adapter->places[at].parent;
    return parent != UINT32_MAX ? adapter->walk[parent] : 0;
}

// Whether clients know a node: the application's root, or one told
// before this walk.
static bool IsKnown(const muiAtspiAdapter* adapter, uint64_t id)
{
    const muiAtspiTold* told = muiIdMapFind(&adapter->toldById, id);
    return id == 0 || (told != nullptr && told->born != adapter->pass);
}

// Marks what was told and is still shown, and which nodes moved: to
// another parent, or out of order among the siblings that stay, kept
// where their told indexes rise. Tells of their leaving first, so the
// indexes of the nodes told added after are those clients end with.
static void MarkSeen(muiAtspiAdapter* adapter, uint32_t count)
{
    uint32_t lastParent = UINT32_MAX;
    int64_t lastIndex = -1;
    for (uint32_t at = 0; at < count; at++)
    {
        muiAtspiTold* told = muiIdMapFind(&adapter->toldById, adapter->walk[at]);
        muiAtspiPlace* place = &adapter->places[at];
        uint64_t parent = ParentAt(adapter, at);
        if (place->parent != lastParent)
        {
            lastParent = place->parent;
            lastIndex = -1;
        }
        place->tell = told == nullptr;
        if (told == nullptr)
        {
            continue;
        }
        told->seen = adapter->pass;
        place->tell = told->parent != parent || (int64_t)told->index <= lastIndex;
        if (!place->tell)
        {
            lastIndex = told->index;
        }
        else if (told->parent == 0 || muiAccessTree_IsShown(adapter->tree, told->parent))
        {
            TellChild(adapter, told->parent, told->id, -1);
        }
    }
}

// Tells of what is no longer shown, the topmost of each part only, and
// forgets it.
static void Forget(muiAtspiAdapter* adapter)
{
    for (uint32_t i = 0; i < adapter->nodes; i++)
    {
        const muiAtspiTold* told = &adapter->told[i];
        const muiAtspiTold* parent = muiIdMapFind(&adapter->toldById, told->parent);
        bool topmost = told->parent == 0 || (parent != nullptr && parent->seen == adapter->pass);
        if (told->id != 0 && told->seen != adapter->pass && topmost)
        {
            TellChild(adapter, told->parent, told->id, -1);
            if (muiAccessTree_Find(adapter->tree, told->id) == nullptr)
            {
                muiAccessNode stand = {.id = told->id};
                const muiAtspiObject object = {adapter, &stand};
                EmitState(adapter->app, &object, "defunct", true);
            }
        }
    }
    for (uint32_t i = 0; i < adapter->nodes; i++)
    {
        muiAtspiTold* told = &adapter->told[i];
        if (told->id != 0 && told->seen != adapter->pass)
        {
            (void)muiIdMapRemove(&adapter->toldById, told->id);
            *told = (muiAtspiTold){0};
            adapter->freeTold[adapter->freeCount++] = i;
        }
    }
}

// Tells of nodes new or moved under parents clients knew, in the walk's
// order, so each parent's in rising index; records where every shown
// node is now.
static void Learn(muiAtspiAdapter* adapter, uint32_t count)
{
    for (uint32_t at = 0; at < count; at++)
    {
        uint64_t id = adapter->walk[at];
        const muiAtspiPlace* place = &adapter->places[at];
        uint64_t parent = ParentAt(adapter, at);
        muiAtspiTold* told = muiIdMapFind(&adapter->toldById, id);
        if (told == nullptr && adapter->freeCount != 0)
        {
            told = &adapter->told[adapter->freeTold[--adapter->freeCount]];
            *told = (muiAtspiTold){.id = id, .born = adapter->pass};
            (void)muiIdMapInsert(&adapter->toldById, id, told);
        }
        if (told == nullptr)
        {
            continue;
        }
        if (place->tell && IsKnown(adapter, parent))
        {
            TellChild(adapter, parent, id, (int32_t)place->index);
            if (parent == 0)
            {
                // A window's root, new under the application: the window
                // is active, as a toolkit says when its window opens.
                muiAccessNode stand;
                const muiAtspiObject window = ObjectOf(adapter, id, &stand);
                const int32_t none[2] = {0, 0};
                EmitOf(adapter->app, &window, INTERFACE_WINDOW, "Activate", "", none, nullptr);
            }
        }
        told->parent = parent;
        told->index = place->index;
        told->seen = adapter->pass;
    }
}

static void TellStructure(muiAtspiAdapter* adapter)
{
    adapter->pass++;
    uint32_t count = Walk(adapter);
    MarkSeen(adapter, count);
    Forget(adapter);
    Learn(adapter, count);
}

// Tells where the shown focus went, when it moved: off the object told
// before, if it is still held, and onto the new one; an active
// descendant's container tells it too, as toolkits do.
static void TellFocus(muiAtspiAdapter* adapter)
{
    uint64_t shown = muiAtspiShownFocus(adapter->tree);
    if (shown == adapter->toldFocus)
    {
        return;
    }
    const muiAtspiObject from = muiAtspiObjectOf(adapter, adapter->toldFocus);
    const muiAtspiObject to = muiAtspiObjectOf(adapter, shown);
    adapter->toldFocus = shown;
    if (from.node != nullptr)
    {
        EmitState(adapter->app, &from, "focused", false);
    }
    if (to.node == nullptr)
    {
        return;
    }
    EmitState(adapter->app, &to, "focused", true);
    uint64_t focus = muiAccessTree_GetFocus(adapter->tree);
    if (shown != focus)
    {
        const muiAtspiObject container = muiAtspiObjectOf(adapter, focus);
        const Any any = {0, nullptr, &to};
        Emit(adapter->app, &container, "ActiveDescendantChanged", "", 0, &any);
    }
}

muiResult muiAtspiAdapter_Apply(muiAtspiAdapter* adapter, const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiAccessChanges changes = {
        .user = adapter, .updated = Updated, .shownChanged = ShownChanged};
    adapter->reshaped = false;
    muiResult status = muiAccessTree_Apply(adapter->tree, update, &changes);
    if (status == mui_success)
    {
        // Walked only when the update may have changed what is shown: a
        // value or a name changing costs no walk.
        if (adapter->reshaped)
        {
            TellStructure(adapter);
        }
        TellFocus(adapter);
    }
    return status;
}

void muiAtspiTellGone(muiAtspiAdapter* adapter)
{
    for (uint32_t i = 0; i < adapter->nodes; i++)
    {
        const muiAtspiTold* told = &adapter->told[i];
        if (told->id != 0 && told->parent == 0)
        {
            TellChild(adapter, 0, told->id, -1);
            muiAccessNode stand = {.id = told->id};
            const muiAtspiObject object = {adapter, &stand};
            EmitState(adapter->app, &object, "defunct", true);
        }
    }
}
