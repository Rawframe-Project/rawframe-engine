// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter (record mui-0008): making it and its
// provider, applying updates, and the virtual ids its nodes have. A
// virtual id is given when a client first sees a node and freed when the
// node goes, the oldest freed given first, so that a client's stale id
// is unlikely to name another node soon.

#include "allocator.h"
#include "android.h"

#include <stdalign.h>
#include <string.h>

#define DEF_COOKIE 0x6D75616Eu // "muan"
#define PROVIDER   "maul.ui.AccessProvider"

muiAndroidAdapterDef muiDefaultAndroidAdapterDef(void)
{
    return (muiAndroidAdapterDef){.cookie = DEF_COOKIE, .nodes = 4096, .scale = 1.0f};
}

static bool IsValid(const muiAndroidAdapterDef* def)
{
    return def->cookie == DEF_COOKIE && muiIsAllocatorValid(&def->allocator) && def->nodes != 0 &&
           def->nodes <= ((uint32_t)1 << 24) && def->env != nullptr && def->view != nullptr &&
           def->action != nullptr && def->scale > 0.0f;
}

// The id map is at most half full.
static uint32_t MapSizeOf(uint32_t nodes)
{
    uint32_t size = 2;
    while (size < 2 * nodes)
    {
        size *= 2;
    }
    return size;
}

static size_t SizeOf(uint32_t nodes)
{
    size_t map = MapSizeOf(nodes);
    return sizeof(muiAndroidAdapter) + 2 * (size_t)nodes * sizeof(uint64_t) +
           map * (sizeof(uint64_t) + sizeof(void*)) +
           ((size_t)nodes + MUI_ANDROID_CHILDREN) * sizeof(jint) + (size_t)nodes * sizeof(uint32_t);
}

static void Lay(muiAndroidAdapter* adapter, unsigned char* block)
{
    uint32_t nodes = adapter->nodes;
    uint32_t map = MapSizeOf(nodes);
    adapter->scratch = (uint64_t*)(block + sizeof(muiAndroidAdapter));
    adapter->nodeOfVirtual = adapter->scratch + nodes;
    uint64_t* keys = adapter->nodeOfVirtual + nodes;
    void** values = (void**)(keys + map);
    muiIdMapInit(&adapter->virtualById, keys, values, map);
    adapter->packed = (jint*)(values + map);
    adapter->freeVirtuals = (uint32_t*)(adapter->packed + nodes + MUI_ANDROID_CHILDREN);
    memset(adapter->nodeOfVirtual, 0, (size_t)nodes * sizeof(uint64_t));
    for (uint32_t i = 0; i < nodes; i++)
    {
        adapter->freeVirtuals[i] = i + 1;
    }
    adapter->freeHead = 0;
    adapter->freeCount = nodes;
}

JNIEnv* muiAndroidEnv(const muiAndroidAdapter* adapter)
{
    JNIEnv* env = nullptr;
    JavaVM* vm = adapter->vm;
    return (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK ? env : nullptr;
}

// Whether Java threw; the exception is cleared.
static bool Threw(JNIEnv* env)
{
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return true;
    }
    return false;
}

// The provider's class through the view's class loader, which holds the
// application's classes on any thread.
static jclass FindProvider(JNIEnv* env, jobject view)
{
    jclass viewClass = (*env)->GetObjectClass(env, view);
    jmethodID getContext =
        (*env)->GetMethodID(env, viewClass, "getContext", "()Landroid/content/Context;");
    jobject context =
        getContext != nullptr ? (*env)->CallObjectMethod(env, view, getContext) : nullptr;
    if (Threw(env) || context == nullptr)
    {
        return nullptr;
    }
    jclass contextClass = (*env)->GetObjectClass(env, context);
    jmethodID getLoader =
        (*env)->GetMethodID(env, contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader =
        getLoader != nullptr ? (*env)->CallObjectMethod(env, context, getLoader) : nullptr;
    if (Threw(env) || loader == nullptr)
    {
        return nullptr;
    }
    jclass loaderClass = (*env)->GetObjectClass(env, loader);
    jmethodID loadClass =
        (*env)->GetMethodID(env, loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = (*env)->NewStringUTF(env, PROVIDER);
    jobject found = loadClass != nullptr && name != nullptr
                        ? (*env)->CallObjectMethod(env, loader, loadClass, name)
                        : nullptr;
    return Threw(env) ? nullptr : (jclass)found;
}

// The provider for the view, its class bound to the native methods.
static muiResult MakeProvider(muiAndroidAdapter* adapter, JNIEnv* env, jobject view)
{
    if ((*env)->PushLocalFrame(env, 16) != JNI_OK)
    {
        (void)Threw(env);
        return mui_errorPlatform;
    }
    jclass found = FindProvider(env, view);
    jmethodID make = found != nullptr
                         ? (*env)->GetMethodID(env, found, "<init>", "(Landroid/view/View;J)V")
                         : nullptr;
    adapter->handle = found != nullptr ? (*env)->GetFieldID(env, found, "handle", "J") : nullptr;
    adapter->send = found != nullptr ? (*env)->GetMethodID(env, found, "send", "(III)V") : nullptr;
    jobject provider = make != nullptr && adapter->handle != nullptr && adapter->send != nullptr &&
                               muiAndroidRegister(env, found)
                           ? (*env)->NewObject(env, found, make, view, (jlong)(intptr_t)adapter)
                           : nullptr;
    bool made = !Threw(env) && provider != nullptr;
    if (made)
    {
        adapter->view = (*env)->NewGlobalRef(env, view);
        adapter->provider = (*env)->NewGlobalRef(env, provider);
        adapter->providerClass = (*env)->NewGlobalRef(env, found);
    }
    (*env)->PopLocalFrame(env, nullptr);
    return made && adapter->view != nullptr && adapter->provider != nullptr &&
                   adapter->providerClass != nullptr
               ? mui_success
               : mui_errorPlatform;
}

static void LetGo(muiAndroidAdapter* adapter, JNIEnv* env)
{
    if (adapter->provider != nullptr)
    {
        (*env)->SetLongField(env, adapter->provider, adapter->handle, 0);
        (*env)->DeleteGlobalRef(env, adapter->provider);
    }
    if (adapter->providerClass != nullptr)
    {
        (*env)->DeleteGlobalRef(env, adapter->providerClass);
    }
    if (adapter->view != nullptr)
    {
        (*env)->DeleteGlobalRef(env, adapter->view);
    }
}

muiResult muiCreateAndroidAdapter(const muiAndroidAdapterDef* def, muiAndroidAdapter** adapterOut)
{
    if (adapterOut != nullptr)
    {
        *adapterOut = nullptr;
    }
    if (def == nullptr || adapterOut == nullptr || !IsValid(def))
    {
        return mui_errorInvalid;
    }
    size_t size = SizeOf(def->nodes);
    unsigned char* block = muiAllocate(&def->allocator, size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    muiAndroidAdapter* adapter = (muiAndroidAdapter*)block;
    *adapter = (muiAndroidAdapter){
        .allocator = def->allocator,
        .blockSize = size,
        .scale = def->scale,
        .action = def->action,
        .user = def->user,
        .nodes = def->nodes,
        .tell = muiAndroidTell,
    };
    Lay(adapter, block);
    JNIEnv* env = def->env;
    muiAccessTreeDef treeDef = muiDefaultAccessTreeDef();
    treeDef.allocator = def->allocator;
    treeDef.nodes = def->nodes;
    muiResult status =
        (*env)->GetJavaVM(env, &adapter->vm) == JNI_OK ? mui_success : mui_errorPlatform;
    status = status == mui_success ? muiCreateAccessTree(&treeDef, &adapter->tree) : status;
    status = status == mui_success ? MakeProvider(adapter, env, def->view) : status;
    if (status != mui_success)
    {
        LetGo(adapter, env);
        muiDestroyAccessTree(adapter->tree);
        muiRelease(&def->allocator, block, size, alignof(max_align_t));
        return status;
    }
    *adapterOut = adapter;
    return mui_success;
}

void muiDestroyAndroidAdapter(muiAndroidAdapter* adapter)
{
    if (adapter == nullptr)
    {
        return;
    }
    JNIEnv* env = muiAndroidEnv(adapter);
    if (env != nullptr)
    {
        LetGo(adapter, env);
    }
    muiDestroyAccessTree(adapter->tree);
    const muiAllocator allocator = adapter->allocator;
    muiRelease(&allocator, adapter, adapter->blockSize, alignof(max_align_t));
}

jint muiAndroidVirtualOf(muiAndroidAdapter* adapter, uint64_t id)
{
    void* held = muiIdMapFind(&adapter->virtualById, id);
    if (held != nullptr)
    {
        return (jint)(uintptr_t)held;
    }
    if (adapter->freeCount == 0)
    {
        return MUI_ANDROID_NO_ID;
    }
    uint32_t virtualId = adapter->freeVirtuals[adapter->freeHead];
    if (!muiIdMapInsert(&adapter->virtualById, id, (void*)(uintptr_t)virtualId))
    {
        return MUI_ANDROID_NO_ID;
    }
    adapter->freeHead = (adapter->freeHead + 1) % adapter->nodes;
    adapter->freeCount--;
    adapter->nodeOfVirtual[virtualId - 1] = id;
    return (jint)virtualId;
}

uint64_t muiAndroidNodeOf(const muiAndroidAdapter* adapter, jint virtualId)
{
    return virtualId >= 1 && (uint32_t)virtualId <= adapter->nodes
               ? adapter->nodeOfVirtual[virtualId - 1]
               : 0;
}

// A node gone frees its virtual id, to the back of the queue.
static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    (void)tree;
    muiAndroidAdapter* adapter = user;
    void* held = muiIdMapRemove(&adapter->virtualById, old->id);
    if (held == nullptr)
    {
        return;
    }
    uint32_t virtualId = (uint32_t)(uintptr_t)held;
    adapter->nodeOfVirtual[virtualId - 1] = 0;
    adapter->freeVirtuals[(adapter->freeHead + adapter->freeCount) % adapter->nodes] = virtualId;
    adapter->freeCount++;
}

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    muiAndroidTellUpdated(user, old, muiAccessTree_Find(tree, old->id));
}

static void ShownChanged(void* user, const muiAccessTree* tree)
{
    (void)tree;
    ((muiAndroidAdapter*)user)->reshaped = true;
}

static void FocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    (void)tree;
    (void)old;
    (void)focus;
    ((muiAndroidAdapter*)user)->focusMoved = true;
}

muiResult muiAndroidAdapter_Apply(muiAndroidAdapter* adapter, const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiAccessChanges changes = {.user = adapter,
                                      .updated = Updated,
                                      .removed = Removed,
                                      .focusMoved = FocusMoved,
                                      .shownChanged = ShownChanged};
    adapter->reshaped = false;
    adapter->focusMoved = false;
    muiResult status = muiAccessTree_Apply(adapter->tree, update, &changes);
    if (status == mui_success)
    {
        muiAndroidTellChanges(adapter);
    }
    return status;
}

const muiAccessTree* muiAndroidAdapter_GetTree(const muiAndroidAdapter* adapter)
{
    return adapter != nullptr ? adapter->tree : nullptr;
}

muiResult muiAndroidAdapter_SetScale(muiAndroidAdapter* adapter, float scale)
{
    if (adapter == nullptr || !(scale > 0.0f))
    {
        return mui_errorInvalid;
    }
    adapter->scale = scale;
    return mui_success;
}

void* muiAndroidAdapter_GetRoot(muiAndroidAdapter* adapter)
{
    return adapter != nullptr ? (void*)adapter->provider : nullptr;
}
