// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter's internals (record mui-0008): the
// adapter, its virtual ids, and the packed record its provider
// (java/maul/ui/AccessProvider.java) reads, whose layout and bits are
// that file's.

#ifndef MAUL_UI_SRC_ANDROID_H
#define MAUL_UI_SRC_ANDROID_H

#include "id_map.h"

#include "maul-ui/access_android.h"

#include <jni.h>

// A packed record's places.
enum
{
    MUI_ANDROID_CLASS = 0,
    MUI_ANDROID_FLAGS = 1,
    MUI_ANDROID_ACTIONS = 2,
    MUI_ANDROID_LEFT = 3,
    MUI_ANDROID_TOP = 4,
    MUI_ANDROID_RIGHT = 5,
    MUI_ANDROID_BOTTOM = 6,
    MUI_ANDROID_PARENT = 7,
    MUI_ANDROID_RANGE = 8,
    MUI_ANDROID_MINIMUM = 9,
    MUI_ANDROID_MAXIMUM = 10,
    MUI_ANDROID_CURRENT = 11,
    MUI_ANDROID_LIVE = 12,
    MUI_ANDROID_COUNT = 13,
    MUI_ANDROID_CHILDREN = 14,
};

// The record's flags.
enum
{
    MUI_ANDROID_CHECKABLE = 1 << 0,
    MUI_ANDROID_CHECKED = 1 << 1,
    MUI_ANDROID_ENABLED = 1 << 2,
    MUI_ANDROID_FOCUSABLE = 1 << 3,
    MUI_ANDROID_FOCUSED = 1 << 4,
    MUI_ANDROID_SELECTED = 1 << 5,
    MUI_ANDROID_SCROLLABLE = 1 << 6,
    MUI_ANDROID_PASSWORD = 1 << 7,
    MUI_ANDROID_EDITABLE = 1 << 8,
    MUI_ANDROID_HEADING = 1 << 9,
    MUI_ANDROID_MULTILINE = 1 << 10,
};

// The provider's actions, by index: offered as bits, asked by index.
enum
{
    MUI_ANDROID_CLICK = 0,
    MUI_ANDROID_FOCUS = 1,
    MUI_ANDROID_CLEAR_FOCUS = 2,
    MUI_ANDROID_SCROLL_BACKWARD = 3,
    MUI_ANDROID_SCROLL_FORWARD = 4,
    MUI_ANDROID_SET_PROGRESS = 5,
    MUI_ANDROID_EXPAND = 6,
    MUI_ANDROID_COLLAPSE = 7,
};

// A node's texts, by kind.
enum
{
    MUI_ANDROID_TEXT = 0,
    MUI_ANDROID_DESCRIPTION = 1,
    MUI_ANDROID_HINT = 2,
    MUI_ANDROID_ROLE = 3,
    MUI_ANDROID_STATE = 4,
};

// android.view.View.NO_ID.
#define MUI_ANDROID_NO_ID (-1)

// Tells clients of an event on a virtual view (muiAndroidTell, through
// the provider, or a test's): its type, and content change types.
typedef void (*muiAndroidTellFunction)(const muiAndroidAdapter* adapter, jint virtualId, jint type,
                                       jint changes);

struct muiAndroidAdapter
{
    muiAllocator allocator;
    size_t blockSize;
    muiAccessTree* tree;
    JavaVM* vm;
    // Global references: the view, the provider, its class.
    jobject view;
    jobject provider;
    jclass providerClass;
    jfieldID handle;
    jmethodID send;
    float scale;
    muiAndroidActionFunction action;
    void* user;
    uint32_t nodes;
    // Room for any node's shown children.
    uint64_t* scratch;
    // The packed record being made: its places and a child each.
    jint* packed;
    // Virtual ids, 1 to nodes, given to nodes as clients first see them:
    // the node of each (0 for a free one), each node's, and the free
    // ones in the order they were freed, so that an id is reused as late
    // as can be.
    uint64_t* nodeOfVirtual;
    muiIdMap virtualById;
    uint32_t* freeVirtuals;
    uint32_t freeHead;
    uint32_t freeCount;
    muiAndroidTellFunction tell;
    // What the update being applied changed: the shown tree, the focus.
    bool reshaped;
    bool focusMoved;
};

// The JNIEnv of this thread.
JNIEnv* muiAndroidEnv(const muiAndroidAdapter* adapter);

// A node's virtual id, given now if it has none; MUI_ANDROID_NO_ID when
// none is free. The node of a virtual id, 0 for none.
jint muiAndroidVirtualOf(muiAndroidAdapter* adapter, uint64_t id);
uint64_t muiAndroidNodeOf(const muiAndroidAdapter* adapter, jint virtualId);

// The provider's native methods (android_jni.c), bound to its class.
bool muiAndroidRegister(JNIEnv* env, jclass providerClass);

// A node's packed record into adapter->packed; how many places it
// fills (android_node.c).
uint32_t muiAndroidPack(muiAndroidAdapter* adapter, const muiAccessNode* node);

// Where a node's text of a kind comes from: its name, or else (and when
// the name is empty) a text of the record's; neither is none.
typedef struct muiAndroidText
{
    bool name;
    const char* text;
} muiAndroidText;

muiAndroidText muiAndroidTextOf(const muiAccessNode* node, int kind);

// The provider's action at an index as the host's, asked of it.
bool muiAndroidAct(const muiAndroidAdapter* adapter, const muiAccessNode* node, int action,
                   float value);

// The events (android_events.c): an updated record, and what the update
// changed once applied; muiAndroidTell sends one through the provider.
void muiAndroidTellUpdated(muiAndroidAdapter* adapter, const muiAccessNode* old,
                           const muiAccessNode* node);
void muiAndroidTellChanges(muiAndroidAdapter* adapter);
void muiAndroidTell(const muiAndroidAdapter* adapter, jint virtualId, jint type, jint changes);

// The deepest shown node under a place in the view's pixels, 0 for none.
uint64_t muiAndroidNodeAt(muiAndroidAdapter* adapter, float x, float y);

#endif // MAUL_UI_SRC_ANDROID_H
