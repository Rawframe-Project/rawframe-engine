// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter's events (record mui-0008), for the
// nodes clients have seen: a content change of a node whose record
// changed (its text or content description when only its name or value
// text did, else unsaid), which also makes Android speak a live node; a
// content change of the subtree when the shown tree may have changed;
// the view focused when the focus moved. Text being edited tells its
// text changed, as a common prefix and suffix in UTF-16, and its
// selection.

#include "access_record.h"
#include "access_text.h"
#include "android.h"

// android.view.accessibility.AccessibilityEvent's types and content
// change types.
#define TYPE_VIEW_FOCUSED           8
#define TYPE_WINDOW_CONTENT_CHANGED 2048
#define CHANGE_UNDEFINED            0
#define CHANGE_SUBTREE              1
#define CHANGE_TEXT                 2
#define CHANGE_CONTENT_DESCRIPTION  4
#define CHANGE_STATE_DESCRIPTION    64

// A virtual id clients have, or none.
static jint SeenOf(const muiAndroidAdapter* adapter, uint64_t id)
{
    void* held = muiIdMapFind(&adapter->virtualById, id);
    return held != nullptr ? (jint)(uintptr_t)held : MUI_ANDROID_NO_ID;
}

// Whether anything of a record but its name and value text changed, as
// clients would read it.
static bool OtherDiffers(const muiAccessNode* old, const muiAccessNode* node)
{
    const uint32_t read = mui_accessCheckable | mui_accessMixed | mui_accessChecked |
                          mui_accessSelected | mui_accessDisabled | mui_accessExpanded |
                          mui_accessFocusable | mui_accessReadOnly | mui_accessScrolls |
                          mui_accessNumeric;
    return old->role != node->role || ((old->flags ^ node->flags) & read) != 0 ||
           old->actions != node->actions || old->value != node->value ||
           old->minimum != node->minimum || old->maximum != node->maximum ||
           old->values.live != node->values.live ||
           muiRecordTextDiffers(old, node, mui_accessDescription) ||
           muiRecordTextDiffers(old, node, mui_accessPlaceholder) ||
           muiRecordTextDiffers(old, node, mui_accessRoleDescription);
}

// The content change types of an updated record, as the provider shows
// its name and value text; -1 for none.
static jint ChangesOf(const muiAccessNode* old, const muiAccessNode* node)
{
    if (OtherDiffers(old, node))
    {
        return CHANGE_UNDEFINED;
    }
    jint changes = 0;
    if (muiRecordNameDiffers(old, node))
    {
        // A text field's name is its hint, which has no change type.
        if (muiAndroidTextOf(node, MUI_ANDROID_DESCRIPTION).name)
        {
            changes |= CHANGE_CONTENT_DESCRIPTION;
        }
        else if (muiAndroidTextOf(node, MUI_ANDROID_TEXT).name)
        {
            changes |= CHANGE_TEXT;
        }
        else
        {
            return CHANGE_UNDEFINED;
        }
    }
    if (muiRecordTextDiffers(old, node, mui_accessValue))
    {
        // The value text is a state where either record shows it so.
        bool state = muiAndroidTextOf(node, MUI_ANDROID_STATE).text != nullptr ||
                     muiAndroidTextOf(old, MUI_ANDROID_STATE).text != nullptr;
        changes |= state ? CHANGE_STATE_DESCRIPTION : CHANGE_TEXT;
    }
    return changes != 0 ? changes : -1;
}

static bool IsTrail(char byte)
{
    return ((unsigned char)byte & 0xC0u) == 0x80u;
}

// The bytes two texts share at their starts and at their ends, apart
// and at characters' starts.
static void SharedOf(const muiAccessText* was, const muiAccessText* now, uint32_t* prefixOut,
                     uint32_t* suffixOut)
{
    uint32_t shorter = was->length < now->length ? was->length : now->length;
    uint32_t prefix = 0;
    while (prefix < shorter && was->bytes[prefix] == now->bytes[prefix])
    {
        prefix++;
    }
    while (prefix > 0 && ((prefix < was->length && IsTrail(was->bytes[prefix])) ||
                          (prefix < now->length && IsTrail(now->bytes[prefix]))))
    {
        prefix--;
    }
    uint32_t suffix = 0;
    while (suffix < shorter - prefix &&
           was->bytes[was->length - 1 - suffix] == now->bytes[now->length - 1 - suffix])
    {
        suffix++;
    }
    while (suffix > 0 && IsTrail(was->bytes[was->length - suffix]))
    {
        suffix--;
    }
    *prefixOut = prefix;
    *suffixOut = suffix;
}

static jint UnitsBefore(const muiAccessText* text, uint32_t byte)
{
    return (jint)muiAccessUtf16Before(text->bytes, byte);
}

static void TellText(const muiAndroidAdapter* adapter, jint seen, const muiAccessNode* old,
                     const muiAccessNode* node)
{
    muiAccessText was = muiAccessValueOf(old);
    muiAccessText now = muiAccessValueOf(node);
    if (muiRecordTextDiffers(old, node, mui_accessValue))
    {
        uint32_t prefix = 0;
        uint32_t suffix = 0;
        SharedOf(&was, &now, &prefix, &suffix);
        jint from = UnitsBefore(&now, prefix);
        const jint numbers[3] = {from, UnitsBefore(&was, was.length - suffix) - from,
                                 UnitsBefore(&now, now.length - suffix) - from};
        adapter->tellText(adapter, seen, MUI_ANDROID_TEXT_CHANGED, numbers, was.bytes, was.length);
    }
    const muiAccessTextMarks* marks = &node->marks;
    if (marks->selected && (!old->marks.selected || old->marks.anchor != marks->anchor ||
                            old->marks.focus != marks->focus))
    {
        const jint numbers[3] = {UnitsBefore(&now, marks->anchor), UnitsBefore(&now, marks->focus),
                                 UnitsBefore(&now, now.length)};
        adapter->tellText(adapter, seen, MUI_ANDROID_SELECTION_CHANGED, numbers, nullptr, 0);
    }
}

void muiAndroidTellUpdated(muiAndroidAdapter* adapter, const muiAccessNode* old,
                           const muiAccessNode* node)
{
    jint seen = SeenOf(adapter, node->id);
    jint changes = seen != MUI_ANDROID_NO_ID ? ChangesOf(old, node) : -1;
    if (changes >= 0)
    {
        adapter->tell(adapter, seen, TYPE_WINDOW_CONTENT_CHANGED, changes);
    }
    if (seen != MUI_ANDROID_NO_ID && muiAccessIsEdited(node))
    {
        TellText(adapter, seen, old, node);
    }
}

void muiAndroidTellChanges(muiAndroidAdapter* adapter)
{
    if (adapter->reshaped)
    {
        uint64_t root = muiAccessTree_GetRoot(adapter->tree);
        jint seen = root != 0 ? SeenOf(adapter, root) : MUI_ANDROID_NO_ID;
        adapter->tell(adapter, seen, TYPE_WINDOW_CONTENT_CHANGED, CHANGE_SUBTREE);
    }
    uint64_t focus = muiRecordActiveFocus(adapter->tree);
    if (adapter->focusMoved && focus != 0)
    {
        jint virtualId = muiAndroidVirtualOf(adapter, focus);
        if (virtualId != MUI_ANDROID_NO_ID)
        {
            adapter->tell(adapter, virtualId, TYPE_VIEW_FOCUSED, 0);
        }
    }
}

void muiAndroidTellText(const muiAndroidAdapter* adapter, jint virtualId, jint type,
                        const jint numbers[3], const char* before, uint32_t length)
{
    JNIEnv* env = muiAndroidEnv(adapter);
    if (env == nullptr || (*env)->PushLocalFrame(env, 4) != JNI_OK)
    {
        return;
    }
    jstring was = muiAndroidStringOf(env, adapter, before, length);
    (*env)->CallVoidMethod(env, adapter->provider, adapter->sendText, virtualId, type, numbers[0],
                           numbers[1], numbers[2], was);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
    (*env)->PopLocalFrame(env, nullptr);
}

void muiAndroidTell(const muiAndroidAdapter* adapter, jint virtualId, jint type, jint changes)
{
    JNIEnv* env = muiAndroidEnv(adapter);
    if (env == nullptr)
    {
        return;
    }
    (*env)->CallVoidMethod(env, adapter->provider, adapter->send, virtualId, type, changes);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
    }
}
