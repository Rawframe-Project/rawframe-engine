// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter's native methods (record mui-0008),
// which maul.ui.AccessProvider calls with the adapter as a handle, bound
// with RegisterNatives so that no JNI name is exported. Texts go to Java
// as UTF-16: the JNI's own UTF-8 is a modified one, which a character
// past the Basic Multilingual Plane breaks.

#include "access_record.h"
#include "access_text.h"
#include "allocator.h"
#include "android.h"

#include <math.h>
#include <stdalign.h>
#include <string.h>

static muiAndroidAdapter* AdapterOf(jlong handle)
{
    return (muiAndroidAdapter*)(intptr_t)handle;
}

// The held, shown node of a virtual id, or none.
static const muiAccessNode* ShownOf(const muiAndroidAdapter* adapter, jint virtualId)
{
    uint64_t id = muiAndroidNodeOf(adapter, virtualId);
    return id != 0 && muiAccessTree_IsShown(adapter->tree, id)
               ? muiAccessTree_Find(adapter->tree, id)
               : nullptr;
}

static jint RootOf(JNIEnv* env, jclass type, jlong handle)
{
    (void)env;
    (void)type;
    muiAndroidAdapter* adapter = AdapterOf(handle);
    uint64_t root = muiAccessTree_GetRoot(adapter->tree);
    return root != 0 ? muiAndroidVirtualOf(adapter, root) : MUI_ANDROID_NO_ID;
}

static jintArray NodeOf(JNIEnv* env, jclass type, jlong handle, jint virtualId)
{
    (void)type;
    muiAndroidAdapter* adapter = AdapterOf(handle);
    const muiAccessNode* node = ShownOf(adapter, virtualId);
    if (node == nullptr)
    {
        return nullptr;
    }
    uint32_t count = muiAndroidPack(adapter, node);
    jintArray packed = (*env)->NewIntArray(env, (jsize)count);
    if (packed != nullptr)
    {
        (*env)->SetIntArrayRegion(env, packed, 0, (jsize)count, adapter->packed);
    }
    return packed;
}

// UTF-8, which the tree holds valid, as UTF-16: never more units than
// bytes.
static jsize Utf16Of(const unsigned char* text, size_t length, jchar* out)
{
    jsize units = 0;
    for (size_t i = 0; i < length;)
    {
        uint32_t lead = text[i];
        size_t extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
        uint32_t point = extra == 0   ? lead
                         : extra == 1 ? lead & 0x1F
                         : extra == 2 ? lead & 0x0F
                                      : lead & 0x07;
        for (size_t k = 1; k <= extra && i + k < length; k++)
        {
            point = (point << 6) | (text[i + k] & 0x3Fu);
        }
        i += extra + 1;
        if (point >= 0x10000)
        {
            point -= 0x10000;
            out[units++] = (jchar)(0xD800 + (point >> 10));
            out[units++] = (jchar)(0xDC00 + (point & 0x3FF));
        }
        else
        {
            out[units++] = (jchar)point;
        }
    }
    return units;
}

jstring muiAndroidStringOf(JNIEnv* env, const muiAndroidAdapter* adapter, const char* text,
                           size_t length)
{
    if (length == 0)
    {
        return nullptr;
    }
    jchar* units = muiAllocate(&adapter->allocator, length * sizeof(jchar), alignof(jchar));
    if (units == nullptr)
    {
        return nullptr;
    }
    jstring string =
        (*env)->NewString(env, units, Utf16Of((const unsigned char*)text, length, units));
    muiRelease(&adapter->allocator, units, length * sizeof(jchar), alignof(jchar));
    return string;
}

// A node's name as a string, nullptr for an empty one.
static jstring NameOf(JNIEnv* env, const muiAndroidAdapter* adapter, uint64_t id)
{
    char small[256];
    size_t length = 0;
    muiResult status = muiAccessTree_GetName(adapter->tree, id, small, sizeof(small), &length);
    if (status == mui_success)
    {
        return muiAndroidStringOf(env, adapter, small, length);
    }
    if (status != mui_errorCapacity)
    {
        return nullptr;
    }
    char* name = muiAllocate(&adapter->allocator, length + 1, 1);
    jstring string = nullptr;
    if (name != nullptr &&
        muiAccessTree_GetName(adapter->tree, id, name, length + 1, &length) == mui_success)
    {
        string = muiAndroidStringOf(env, adapter, name, length);
    }
    if (name != nullptr)
    {
        muiRelease(&adapter->allocator, name, length + 1, 1);
    }
    return string;
}

static jstring TextOf(JNIEnv* env, jclass type, jlong handle, jint virtualId, jint kind)
{
    (void)type;
    const muiAndroidAdapter* adapter = AdapterOf(handle);
    const muiAccessNode* node = ShownOf(adapter, virtualId);
    if (node == nullptr)
    {
        return nullptr;
    }
    muiAndroidText source = muiAndroidTextOf(node, kind);
    jstring name = source.name ? NameOf(env, adapter, node->id) : nullptr;
    if (name != nullptr || source.text == nullptr)
    {
        return name;
    }
    return muiAndroidStringOf(env, adapter, source.text, strlen(source.text));
}

static jboolean Act(JNIEnv* env, jclass type, jlong handle, jint virtualId, jint action,
                    jfloat value)
{
    (void)env;
    (void)type;
    const muiAndroidAdapter* adapter = AdapterOf(handle);
    const muiAccessNode* node = ShownOf(adapter, virtualId);
    return node != nullptr && muiAndroidAct(adapter, node, action, value) ? JNI_TRUE : JNI_FALSE;
}

static jint FocusOf(JNIEnv* env, jclass type, jlong handle)
{
    (void)env;
    (void)type;
    muiAndroidAdapter* adapter = AdapterOf(handle);
    uint64_t focus = muiRecordActiveFocus(adapter->tree);
    return focus != 0 ? muiAndroidVirtualOf(adapter, focus) : MUI_ANDROID_NO_ID;
}

static jint NodeAt(JNIEnv* env, jclass type, jlong handle, jfloat x, jfloat y)
{
    (void)env;
    (void)type;
    muiAndroidAdapter* adapter = AdapterOf(handle);
    uint64_t at = muiAndroidNodeAt(adapter, x, y);
    return at != 0 ? muiAndroidVirtualOf(adapter, at) : MUI_ANDROID_NO_ID;
}

// A move through a node's text: the segment passed and the selection
// after, in UTF-16, or nullptr.
static jintArray Traverse(JNIEnv* env, jclass type, jlong handle, jint virtualId, jint granularity,
                          jboolean forward, jboolean extend, jint cursor)
{
    (void)type;
    const muiAndroidAdapter* adapter = AdapterOf(handle);
    const muiAccessNode* node = ShownOf(adapter, virtualId);
    jint moved[4] = {0};
    if (node == nullptr || !muiAndroidTraverse(adapter, node, granularity, forward == JNI_TRUE,
                                               extend == JNI_TRUE, cursor, moved))
    {
        return nullptr;
    }
    jintArray out = (*env)->NewIntArray(env, 4);
    if (out != nullptr)
    {
        (*env)->SetIntArrayRegion(env, out, 0, 4, moved);
    }
    return out;
}

static jboolean Select(JNIEnv* env, jclass type, jlong handle, jint virtualId, jint start, jint end)
{
    (void)env;
    (void)type;
    const muiAndroidAdapter* adapter = AdapterOf(handle);
    const muiAccessNode* node = ShownOf(adapter, virtualId);
    return node != nullptr && muiAndroidSelect(adapter, node, start, end) ? JNI_TRUE : JNI_FALSE;
}

// The new text read as UTF-16, past the JNI's modified UTF-8.
static jboolean SetText(JNIEnv* env, jclass type, jlong handle, jint virtualId, jstring text)
{
    (void)type;
    const muiAndroidAdapter* adapter = AdapterOf(handle);
    const muiAccessNode* node = ShownOf(adapter, virtualId);
    if (node == nullptr || text == nullptr)
    {
        return JNI_FALSE;
    }
    jsize count = (*env)->GetStringLength(env, text);
    size_t size = ((size_t)count + 1) * sizeof(jchar);
    jchar* units = muiAllocate(&adapter->allocator, size, alignof(jchar));
    if (units == nullptr)
    {
        return JNI_FALSE;
    }
    (*env)->GetStringRegion(env, text, 0, count, units);
    bool done = muiAndroidSetText(adapter, node, units, count);
    muiRelease(&adapter->allocator, units, size, alignof(jchar));
    return done ? JNI_TRUE : JNI_FALSE;
}

// Where each UTF-16 unit of a node's text from a start up to a length
// is, in the view's pixels: left, top, right and bottom each, NaN for a
// unit with none; nullptr for text without clusters, a length past
// Android's most or memory run out.
static jfloatArray CharacterBoxes(JNIEnv* env, jclass type, jlong handle, jint virtualId,
                                  jint start, jint length)
{
    (void)type;
    const muiAndroidAdapter* adapter = AdapterOf(handle);
    const muiAccessNode* node = ShownOf(adapter, virtualId);
    if (node == nullptr || start < 0 || length <= 0 || length > MUI_ANDROID_MOST_LOCATIONS)
    {
        return nullptr;
    }
    size_t size = (size_t)length * sizeof(muiRect);
    muiRect* rects = muiAllocate(&adapter->allocator, size, alignof(muiRect));
    jfloat* boxes =
        muiAllocate(&adapter->allocator, (size_t)length * 4 * sizeof(jfloat), alignof(jfloat));
    jfloatArray out = nullptr;
    if (rects != nullptr && boxes != nullptr &&
        muiAccessUnitRects(adapter->tree, node->id, (uint32_t)start, (uint32_t)length,
                           &adapter->allocator, rects))
    {
        float scale = adapter->scale;
        for (jint i = 0; i < length; i++)
        {
            const muiRect* r = &rects[i];
            bool none = r->width < 0.0f;
            boxes[4 * i] = none ? NAN : r->x * scale;
            boxes[4 * i + 1] = none ? NAN : r->y * scale;
            boxes[4 * i + 2] = none ? NAN : (r->x + r->width) * scale;
            boxes[4 * i + 3] = none ? NAN : (r->y + r->height) * scale;
        }
        out = (*env)->NewFloatArray(env, length * 4);
        if (out != nullptr)
        {
            (*env)->SetFloatArrayRegion(env, out, 0, length * 4, boxes);
        }
    }
    if (rects != nullptr)
    {
        muiRelease(&adapter->allocator, rects, size, alignof(muiRect));
    }
    if (boxes != nullptr)
    {
        muiRelease(&adapter->allocator, boxes, (size_t)length * 4 * sizeof(jfloat),
                   alignof(jfloat));
    }
    return out;
}

bool muiAndroidRegister(JNIEnv* env, jclass providerClass)
{
    const JNINativeMethod methods[] = {
        {"rootOf", "(J)I", (void*)RootOf},
        {"nodeOf", "(JI)[I", (void*)NodeOf},
        {"textOf", "(JII)Ljava/lang/String;", (void*)TextOf},
        {"act", "(JIIF)Z", (void*)Act},
        {"focusOf", "(J)I", (void*)FocusOf},
        {"nodeAt", "(JFF)I", (void*)NodeAt},
        {"traverse", "(JIIZZI)[I", (void*)Traverse},
        {"select", "(JIII)Z", (void*)Select},
        {"setText", "(JILjava/lang/String;)Z", (void*)SetText},
        {"characterBoxes", "(JIII)[F", (void*)CharacterBoxes},
    };
    jint count = (jint)(sizeof(methods) / sizeof(methods[0]));
    if ((*env)->RegisterNatives(env, providerClass, methods, count) != JNI_OK)
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    return true;
}
