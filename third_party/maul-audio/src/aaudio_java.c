// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Java half through JNI. Each call attaches the calling thread to
// the VM when it is not attached and detaches it again afterwards, so
// the host's threads are left as they were. A pending Java exception is
// cleared and the call treated as failed.

#include "aaudio_java.h"

#include <jni.h>

static JNIEnv* Enter(JavaVM* vm, bool* attached)
{
    JNIEnv* env = nullptr;
    *attached = false;
    jint got = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (got == JNI_EDETACHED)
    {
        if ((*vm)->AttachCurrentThread(vm, &env, nullptr) != JNI_OK)
        {
            return nullptr;
        }
        *attached = true;
    }
    else if (got != JNI_OK)
    {
        return nullptr;
    }
    return env;
}

static void Leave(JavaVM* vm, bool attached)
{
    if (attached)
    {
        jint detached = (*vm)->DetachCurrentThread(vm);
        (void)detached;
    }
}

// Whether a Java exception is pending; clears it.
static bool Thrown(JNIEnv* env)
{
    if (!(*env)->ExceptionCheck(env))
    {
        return false;
    }
    (*env)->ExceptionClear(env);
    return true;
}

// The signals' address as the Java object holds it.
typedef union SignalsLong
{
    jlong value;
    maudAaudioSignals* signals;
} SignalsLong;

static_assert(sizeof(jlong) >= sizeof(maudAaudioSignals*), "a jlong holds an address");

// The native methods of maul.audio.Devices: Android's devices changed,
// and its audio focus did.
static void JNICALL Raise(JNIEnv* env, jclass type, jlong signals)
{
    (void)env;
    (void)type;
    SignalsLong held = {.value = signals};
    atomic_store_explicit(&held.signals->changed, true, memory_order_release);
}

static void JNICALL Focus(JNIEnv* env, jclass type, jlong signals, jint change)
{
    (void)env;
    (void)type;
    SignalsLong held = {.value = signals};
    atomic_store_explicit(&held.signals->focus, change, memory_order_release);
}

// The library's class, through the Context's class loader, which knows
// the application's classes on any thread.
static jclass LoadDevices(JNIEnv* env, jobject context)
{
    jclass contextType = (*env)->GetObjectClass(env, context);
    jmethodID getLoader =
        (*env)->GetMethodID(env, contextType, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (Thrown(env) || getLoader == nullptr)
    {
        return nullptr;
    }
    jobject loader = (*env)->CallObjectMethod(env, context, getLoader);
    jclass loaderType = (*env)->FindClass(env, "java/lang/ClassLoader");
    if (Thrown(env) || loader == nullptr || loaderType == nullptr)
    {
        return nullptr;
    }
    jmethodID loadClass =
        (*env)->GetMethodID(env, loaderType, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = (*env)->NewStringUTF(env, "maul.audio.Devices");
    if (Thrown(env) || loadClass == nullptr || name == nullptr)
    {
        return nullptr;
    }
    jclass devices = (jclass)(*env)->CallObjectMethod(env, loader, loadClass, name);
    return Thrown(env) ? nullptr : devices;
}

// Finds the class's methods and makes the object; false when any fails.
static bool Make(maudAaudio* aaudio, JNIEnv* env, jobject context)
{
    jclass type = LoadDevices(env, context);
    if (type == nullptr)
    {
        return false;
    }
    static const JNINativeMethod natives[] = {{"raise", "(J)V", (void*)Raise},
                                              {"focus", "(JI)V", (void*)Focus}};
    if ((*env)->RegisterNatives(env, type, natives, 2) != JNI_OK || Thrown(env))
    {
        return false;
    }
    maudAaudioJava* java = &aaudio->java;
    jmethodID make = (*env)->GetMethodID(env, type, "<init>", "(Landroid/content/Context;J)V");
    java->list = (*env)->GetMethodID(env, type, "list", "(Z)[Ljava/lang/Object;");
    java->mayRecord = (*env)->GetMethodID(env, type, "mayRecord", "()Z");
    java->askToRecord = (*env)->GetMethodID(env, type, "askToRecord", "()V");
    java->requestFocus = (*env)->GetMethodID(env, type, "requestFocus", "(IZ)I");
    java->spatializer = (*env)->GetMethodID(env, type, "spatializer", "()I");
    java->close = (*env)->GetMethodID(env, type, "close", "()V");
    if (Thrown(env) || make == nullptr || java->list == nullptr || java->mayRecord == nullptr ||
        java->askToRecord == nullptr || java->requestFocus == nullptr ||
        java->spatializer == nullptr || java->close == nullptr)
    {
        return false;
    }
    SignalsLong held = {.value = 0};
    held.signals = &aaudio->signals;
    jobject devices = (*env)->NewObject(env, type, make, context, held.value);
    if (Thrown(env) || devices == nullptr)
    {
        return false;
    }
    java->context = (*env)->NewGlobalRef(env, context);
    java->devices = (*env)->NewGlobalRef(env, devices);
    return java->context != nullptr && java->devices != nullptr;
}

bool maudAaudioOpenJava(maudAaudio* aaudio, void* vm, void* context)
{
    aaudio->java = (maudAaudioJava){.vm = (JavaVM*)vm};
    bool attached = false;
    JNIEnv* env = Enter(aaudio->java.vm, &attached);
    if (env == nullptr)
    {
        return false;
    }
    bool made = false;
    if ((*env)->PushLocalFrame(env, 16) == JNI_OK)
    {
        made = Make(aaudio, env, (jobject)context);
        (*env)->PopLocalFrame(env, nullptr);
    }
    Leave(aaudio->java.vm, attached);
    aaudio->hasJava = made;
    if (!made)
    {
        maudAaudioCloseJava(aaudio);
    }
    return made;
}

void maudAaudioCloseJava(maudAaudio* aaudio)
{
    maudAaudioJava* java = &aaudio->java;
    bool attached = false;
    JNIEnv* env = java->vm != nullptr ? Enter(java->vm, &attached) : nullptr;
    if (env == nullptr)
    {
        return;
    }
    if (java->devices != nullptr)
    {
        (*env)->CallVoidMethod(env, java->devices, java->close);
        Thrown(env);
        (*env)->DeleteGlobalRef(env, java->devices);
    }
    if (java->context != nullptr)
    {
        (*env)->DeleteGlobalRef(env, java->context);
    }
    Leave(java->vm, attached);
    *java = (maudAaudioJava){0};
    aaudio->hasJava = false;
}

// Hands each device of one listed pair of arrays to listed.
static void Walk(maudAaudio* aaudio, JNIEnv* env, jobjectArray pair, maudDirection direction,
                 void (*listed)(maudAaudio* aaudio, maudDirection direction,
                                const maudAaudioListing* listing))
{
    jintArray numbers = (jintArray)(*env)->GetObjectArrayElement(env, pair, 0);
    jobjectArray texts = (jobjectArray)(*env)->GetObjectArrayElement(env, pair, 1);
    if (Thrown(env) || numbers == nullptr || texts == nullptr)
    {
        return;
    }
    jsize count = (*env)->GetArrayLength(env, numbers) / 5;
    jint* values = (*env)->GetIntArrayElements(env, numbers, nullptr);
    if (values == nullptr)
    {
        Thrown(env);
        return;
    }
    for (jsize i = 0; i < count; ++i)
    {
        jstring address = (jstring)(*env)->GetObjectArrayElement(env, texts, 2 * i);
        jstring product = (jstring)(*env)->GetObjectArrayElement(env, texts, 2 * i + 1);
        const char* addressText =
            address != nullptr ? (*env)->GetStringUTFChars(env, address, nullptr) : nullptr;
        const char* productText =
            product != nullptr ? (*env)->GetStringUTFChars(env, product, nullptr) : nullptr;
        if (!Thrown(env))
        {
            maudAaudioListing listing = {
                .id = values[5 * i],
                .type = values[5 * i + 1],
                .channels = values[5 * i + 2],
                .lowRate = values[5 * i + 3],
                .highRate = values[5 * i + 4],
                .address = addressText != nullptr ? addressText : "",
                .product = productText != nullptr ? productText : "",
            };
            listed(aaudio, direction, &listing);
        }
        if (addressText != nullptr)
        {
            (*env)->ReleaseStringUTFChars(env, address, addressText);
        }
        if (productText != nullptr)
        {
            (*env)->ReleaseStringUTFChars(env, product, productText);
        }
        (*env)->DeleteLocalRef(env, address);
        (*env)->DeleteLocalRef(env, product);
    }
    (*env)->ReleaseIntArrayElements(env, numbers, values, JNI_ABORT);
}

void maudAaudioListJava(maudAaudio* aaudio, maudDirection direction,
                        void (*listed)(maudAaudio* aaudio, maudDirection direction,
                                       const maudAaudioListing* listing))
{
    maudAaudioJava* java = &aaudio->java;
    bool attached = false;
    JNIEnv* env = aaudio->hasJava ? Enter(java->vm, &attached) : nullptr;
    if (env == nullptr)
    {
        return;
    }
    if ((*env)->PushLocalFrame(env, 16) == JNI_OK)
    {
        jobjectArray pair = (jobjectArray)(*env)->CallObjectMethod(
            env, java->devices, java->list, (jboolean)(direction == maud_directionOutput));
        if (!Thrown(env) && pair != nullptr)
        {
            Walk(aaudio, env, pair, direction, listed);
        }
        (*env)->PopLocalFrame(env, nullptr);
    }
    Leave(java->vm, attached);
}

bool maudAaudioMayRecord(maudAaudio* aaudio)
{
    maudAaudioJava* java = &aaudio->java;
    bool attached = false;
    JNIEnv* env = aaudio->hasJava ? Enter(java->vm, &attached) : nullptr;
    if (env == nullptr)
    {
        return true;
    }
    jboolean may = (*env)->CallBooleanMethod(env, java->devices, java->mayRecord);
    bool known = !Thrown(env);
    Leave(java->vm, attached);
    return !known || may == JNI_TRUE;
}

void maudAaudioAskToRecord(maudAaudio* aaudio)
{
    maudAaudioJava* java = &aaudio->java;
    bool attached = false;
    JNIEnv* env = aaudio->hasJava ? Enter(java->vm, &attached) : nullptr;
    if (env == nullptr)
    {
        return;
    }
    (*env)->CallVoidMethod(env, java->devices, java->askToRecord);
    Thrown(env);
    Leave(java->vm, attached);
}

int32_t maudAaudioRequestFocusJava(maudAaudio* aaudio, int32_t kind, bool call)
{
    maudAaudioJava* java = &aaudio->java;
    bool attached = false;
    JNIEnv* env = aaudio->hasJava ? Enter(java->vm, &attached) : nullptr;
    if (env == nullptr)
    {
        return 0;
    }
    jint result =
        (*env)->CallIntMethod(env, java->devices, java->requestFocus, (jint)kind, (jboolean)call);
    if (Thrown(env))
    {
        result = 0;
    }
    Leave(java->vm, attached);
    return result;
}

int32_t maudAaudioSpatializerJava(maudAaudio* aaudio)
{
    maudAaudioJava* java = &aaudio->java;
    bool attached = false;
    JNIEnv* env = aaudio->hasJava ? Enter(java->vm, &attached) : nullptr;
    if (env == nullptr)
    {
        return -1;
    }
    jint state = (*env)->CallIntMethod(env, java->devices, java->spatializer);
    if (Thrown(env))
    {
        state = -1;
    }
    Leave(java->vm, attached);
    return state;
}
