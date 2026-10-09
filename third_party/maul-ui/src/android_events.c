// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter's events (record mui-0008), for the
// nodes clients have seen: a content change of a node whose record
// changed (its text or content description when only its name or value
// text did, else unsaid), which also makes Android speak a live node; a
// content change of the subtree when the shown tree may have changed;
// the view focused when the focus moved.

#include "access_record.h"
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

void muiAndroidTellUpdated(muiAndroidAdapter* adapter, const muiAccessNode* old,
                           const muiAccessNode* node)
{
    jint seen = SeenOf(adapter, node->id);
    jint changes = seen != MUI_ANDROID_NO_ID ? ChangesOf(old, node) : -1;
    if (changes >= 0)
    {
        adapter->tell(adapter, seen, TYPE_WINDOW_CONTENT_CHANGED, changes);
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
    uint64_t focus = muiAccessTree_GetFocus(adapter->tree);
    if (adapter->focusMoved && focus != 0)
    {
        jint virtualId = muiAndroidVirtualOf(adapter, focus);
        if (virtualId != MUI_ANDROID_NO_ID)
        {
            adapter->tell(adapter, virtualId, TYPE_VIEW_FOCUSED, 0);
        }
    }
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
