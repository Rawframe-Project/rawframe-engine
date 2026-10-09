// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the Android backend keeps (mwin-0026): the activity the program
// shows in, and the Java side reached through it; the activity's native
// window and input queue; what drives the frames; whether the activity
// is started and whether the program was told it stopped; and the one
// window, with what the program was last told of it. Everything runs on
// the main thread.

#ifndef MAUL_WINDOW_SRC_ANDROID_H
#define MAUL_WINDOW_SRC_ANDROID_H

#include "android_copy.h"
#include "android_motion.h"
#include "android_pad_map.h"
#include "answer.h"
#include "core.h"

#include <android/choreographer.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <jni.h>
#include <limits.h>

typedef struct mwinAndroidPlatform mwinAndroidPlatform;

// The window: whether its first surface came (mwin_eventWindowCreated
// posted) and whether its surface is gone; its size in pixels, scale and
// focus as last posted; whether it accepts text and where its caret is;
// its safe area and the part the keyboard covers, as last posted.
typedef struct mwinAndroidWindow
{
    bool created;
    bool lost;
    uint32_t width;
    uint32_t height;
    float scale;
    bool focused;
    bool textInput;
    mwinRect caret;
    mwinInsets safeArea;
    mwinRect covered;
    // The cursor asked for: a shape, or a cursor made from images when
    // cursorImage is live (mwin-0027).
    mwinCursorShape cursorShape;
    mwinCursorId cursorImage;
    // Whether the window was told its monitor.
    bool displayTold;
} mwinAndroidWindow;

// The window's insets as the activity last told them, in pixels: the
// system's bars and cutouts on each side, and the input method's at the
// bottom.
typedef struct mwinAndroidInsets
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t keyboard;
} mwinAndroidInsets;

// The Java side, reached through the first activity: the main thread's
// JNI environment, the activity's class (a global reference) and its
// field that holds the running program (maul.window.Activity.program),
// and the library's methods that show the keyboard and drop the input
// method's composition; its services and facts helpers
// (maul.window.Services and maul.window.Facts, global references) and
// their methods; the activity's methods for the pointer's icon.
typedef struct mwinAndroidJava
{
    JNIEnv* env;
    jclass activityClass;
    jfieldID program;
    jmethodID showKeyboard;
    jmethodID restartInput;
    jclass services;
    jmethodID writeClipboard;
    jmethodID readClipboard;
    jmethodID openUrl;
    jclass facts;
    jmethodID readFacts;
    jmethodID readLocales;
    jmethodID showPointerShape;
    jmethodID showPointer;
    jmethodID makePointer;
} mwinAndroidJava;

// The activity's display as the one monitor: the library's Java helper
// (maul.window.Screen, a global reference) and its methods, the
// monitor's slot (-1 before it is read) and when it was last read.
typedef struct mwinAndroidScreen
{
    jclass screen;
    jmethodID readFacts;
    jmethodID readName;
    int32_t monitor;
    uint64_t readNs;
} mwinAndroidScreen;

// The key codes the backend keeps a state of: the keyboard's usages up
// to the right Meta key.
#define MWIN_ANDROID_KEY_CODES 256

// The keys: what each printing key typed when last pressed (0 before),
// the keys held, the dead key waiting for the next (0 when none); the
// Java side's key character maps (android.view.KeyCharacterMap, a global
// reference, with its load, get and getDeadChar).
typedef struct mwinAndroidKeys
{
    mwinKey meanings[MWIN_ANDROID_KEY_CODES];
    uint8_t held[MWIN_ANDROID_KEY_CODES / 8];
    int32_t accent;
    jclass maps;
    jmethodID load;
    jmethodID get;
    jmethodID deadChar;
} mwinAndroidKeys;

// The gamepads the backend follows at most.
#define MWIN_ANDROID_PADS 16

// A gamepad: Android's device id, the core's slot, its layout, and its
// battery as last told.
typedef struct mwinAndroidPad
{
    int32_t device;
    uint32_t slot;
    mwinAndroidPadLayout layout;
    int8_t battery;
    // The latest acceleration and rotation rate its sensors gave.
    float acceleration[3];
    float rotationRate[3];
} mwinAndroidPad;

// The gamepads followed, and when they were last looked for; the
// library's Java helper (maul.window.Gamepads, a global reference) and
// its methods, and mwinAndroidPadKeys as a Java array (a global
// reference).
typedef struct mwinAndroidPads
{
    mwinAndroidPad pads[MWIN_ANDROID_PADS];
    uint32_t count;
    uint64_t lookedNs;
    jclass type;
    jmethodID list;
    jmethodID name;
    jmethodID describe;
    jmethodID ranges;
    jmethodID battery;
    jmethodID rumble;
    jmethodID hasMotion;
    jmethodID motion;
    jintArray keys;
} mwinAndroidPads;

// The file dialog: the library's Java helper (maul.window.Documents, a
// global reference) and its picker; the number of the last dialog, whose
// low byte its picker's request code carries; the request the picker
// answers; and the copy of its documents.
typedef struct mwinAndroidDocuments
{
    jclass type;
    jmethodID open;
    uint32_t number;
    mwinServiceAnswer to;
    mwinAndroidCopy copy;
} mwinAndroidDocuments;

// Drops: while the last drop's documents are copied, the copy, its
// text (UTF-8 from the allocator, or null) and the place it was made.
typedef struct mwinAndroidDrops
{
    mwinAndroidCopy copy;
    char* text;
    size_t textLength;
    mwinPosition place;
} mwinAndroidDrops;

// Accessibility: the library's Java helper (maul.window.Accessibility,
// a global reference) and its methods; the program's root and the
// activity's view, global references or null; and the virtual view
// touch exploration hovers, or -1.
typedef struct mwinAndroidAccessibility
{
    jclass type;
    jmethodID viewOf;
    jmethodID changed;
    jmethodID explore;
    jobject root;
    jobject view;
    jint hovered;
} mwinAndroidAccessibility;

struct mwinAndroidPlatform
{
    mwinContext* context;
    mwinAndroidJava java;
    // The activity the program shows in, null between activities; its
    // native window (acquired) and input queue, null when it has none;
    // its configuration.
    ANativeActivity* activity;
    ANativeWindow* nativeWindow;
    AInputQueue* queue;
    AConfiguration* configuration;
    // The main thread's looper and choreographer; whether a frame
    // callback waits, which nothing can take back: a program that ends
    // meanwhile leaves the context for it to free.
    ALooper* looper;
    AChoreographer* choreographer;
    bool framePosted;
    bool ended;
    // Between the activity's onStart and onStop; whether the program was
    // told the application stopped running; whether the activity's window
    // has the focus.
    bool started;
    bool suspended;
    bool focused;
    // The window's slot, -1 when there is none.
    int32_t slot;
    mwinAndroidWindow window;
    // Its pointers and the keys; the insets the activity told; the
    // gamepads.
    mwinAndroidPointers pointers;
    mwinAndroidKeys keys;
    mwinAndroidInsets insets;
    mwinAndroidPads pads;
    mwinAndroidDocuments documents;
    mwinAndroidDrops drops;
    mwinAndroidAccessibility accessibility;
    mwinAndroidScreen screen;
    // When the facts were last read.
    uint64_t factsReadNs;
};

// The platform of a context whose backend is Android.
mwinAndroidPlatform* mwinAndroidPlatformOf(const mwinContext* context);

// A class of the application or the library, as a local reference, or
// null.
jclass mwinAndroidLoadClass(JNIEnv* env, ANativeActivity* activity, const char* name);

// The running program as maul.window.Activity.program holds it, or null.
mwinAndroidPlatform* mwinAndroidProgramOf(jlong program);

// Nanoseconds on the clock input events count (CLOCK_MONOTONIC).
uint64_t mwinAndroidNow(void);

// The scale the activity's configuration gives: its density over 160.
float mwinAndroidScale(const mwinAndroidPlatform* platform);

// The activity's native window came or went: the window, if any, is
// created at its first surface, its surface lost and restored after
// (android_window.c). Its size or scale may have changed; the activity's
// focus may have.
void mwinAndroidSurfaceCame(mwinAndroidPlatform* platform);
void mwinAndroidSurfaceWent(mwinAndroidPlatform* platform);
void mwinAndroidReadSize(mwinAndroidPlatform* platform);
void mwinAndroidPostFocus(mwinAndroidPlatform* platform);

// A key's press or release, as an event of the input queue or an input
// method gives it: Android's action, key code, scan code (0 for none),
// meta state, device, whether it repeats, and when.
typedef struct mwinAndroidKey
{
    int32_t action;
    int32_t keyCode;
    int32_t scanCode;
    int32_t meta;
    int32_t device;
    bool repeat;
    uint64_t timeNs;
} mwinAndroidKey;

// Input (android_input.c): what the Java side's key maps and the double
// tap's time are, found at the start and let go at the stop; an event of
// the input queue, posted to the window: whether the program took it,
// else Android acts on it; a key, from either; what a key means; the input forgotten when the
// application suspends.
bool mwinAndroidFindInput(mwinAndroidPlatform* platform);
void mwinAndroidLoseInput(mwinAndroidPlatform* platform);
bool mwinAndroidInput(mwinAndroidPlatform* platform, const AInputEvent* event);
bool mwinAndroidKeyEvent(mwinAndroidPlatform* platform, const mwinAndroidKey* key);
mwinKey mwinAndroidMapKeyCode(const mwinAndroidPlatform* platform, mwinKeyCode code);
void mwinAndroidForgetInput(mwinAndroidPlatform* platform);

// Gamepads (android_pad.c): the Java helper found, at the start; the
// gamepads looked for every half second and when an event comes from
// one not followed; an event of a gamepad followed, true when it was
// one; the motors run; the motion sensors turned on or off; everything
// let go, the motors and sensors stopped.
bool mwinAndroidFindPads(mwinAndroidPlatform* platform, ANativeActivity* activity);
void mwinAndroidPumpPads(mwinAndroidPlatform* platform, uint64_t nowNs);
bool mwinAndroidPadInput(mwinAndroidPlatform* platform, const AInputEvent* event);
mwinResult mwinAndroidRumble(mwinAndroidPlatform* platform, uint32_t slot, float low, float high,
                             uint32_t durationMs);
mwinResult mwinAndroidSetMotion(mwinAndroidPlatform* platform, uint32_t slot, bool enabled);
void mwinAndroidStopPads(mwinAndroidPlatform* platform);

// File dialogs (android_dialog.c): the Java helper found and its
// function registered, and the copies of earlier runs removed, at the
// start; the copying let go at the stop; a dialog asked for (-1 while
// it waits for the picker, else its outcome); its documents copied a
// step further between frames, within a budget of bytes.
bool mwinAndroidFindDialogs(mwinAndroidPlatform* platform, ANativeActivity* activity);
void mwinAndroidStopDialogs(mwinAndroidPlatform* platform);
int mwinAndroidAskDialog(mwinAndroidPlatform* platform, uint32_t slot, uint32_t request);
void mwinAndroidPumpDialogs(mwinAndroidPlatform* platform, uint32_t* budget);

// Drops (android_drop.c): the Java listener's functions registered and
// earlier runs' copies removed, at the start; the copy let go at the
// stop; a drop's documents copied a step further between frames, within
// a budget of bytes.
bool mwinAndroidFindDrops(mwinAndroidPlatform* platform, ANativeActivity* activity);
void mwinAndroidStopDrops(mwinAndroidPlatform* platform);
void mwinAndroidPumpDrops(mwinAndroidPlatform* platform, uint32_t* budget);

// Accessibility (android_access.c): the Java helper found at the start
// and everything let go at the stop; an activity's view taken when it
// starts and let go when it leaves; the root let go with the window; the
// root set (the request's outcome); a touch exploration hover taken for
// the root's tree, true when it was.
bool mwinAndroidFindAccessibility(mwinAndroidPlatform* platform, ANativeActivity* activity);
void mwinAndroidStopAccessibility(mwinAndroidPlatform* platform);
void mwinAndroidJoinAccessibility(mwinAndroidPlatform* platform);
void mwinAndroidLeaveAccessibility(mwinAndroidPlatform* platform);
void mwinAndroidForgetAccessibility(mwinAndroidPlatform* platform);
mwinOutcome mwinAndroidSetAccessibilityRoot(mwinAndroidPlatform* platform, void* root);
bool mwinAndroidExplore(mwinAndroidPlatform* platform, const AInputEvent* event);

// The system's facts and locales (android_system.c): the Java helper
// found at the start and let go at the stop; everything read (when an
// activity starts or changes its configuration); the power read every
// two seconds.
bool mwinAndroidFindFacts(mwinAndroidPlatform* platform, ANativeActivity* activity);
void mwinAndroidStopFacts(mwinAndroidPlatform* platform);
void mwinAndroidReadSystem(mwinAndroidPlatform* platform);
void mwinAndroidPumpFacts(mwinAndroidPlatform* platform, uint64_t nowNs);

// The clipboard and services (android_services.c): the Java helper
// found at the start and let go at the stop; the clipboard written and
// read, an address opened, the display kept awake or not.
bool mwinAndroidFindServices(mwinAndroidPlatform* platform, ANativeActivity* activity);
void mwinAndroidStopServices(mwinAndroidPlatform* platform);
mwinOutcome mwinAndroidWriteClipboard(const mwinAndroidPlatform* platform);
mwinOutcome mwinAndroidReadClipboard(const mwinAndroidPlatform* platform);
mwinOutcome mwinAndroidOpenUrl(const mwinAndroidPlatform* platform, const mwinRequest* request);
void mwinAndroidApplyAwake(const mwinAndroidPlatform* platform, bool awake);

// The mouse pointer's icon over the view (android_cursor.c): the Java
// activity's methods found at the start; the icon shown again for a new
// activity; the shape and image requests; the backend's releaseCursor.
bool mwinAndroidFindCursors(mwinAndroidPlatform* platform);
void mwinAndroidApplyCursor(mwinAndroidPlatform* platform);
mwinOutcome mwinAndroidSetCursorShape(mwinAndroidPlatform* platform, mwinCursorShape shape);
mwinOutcome mwinAndroidSetCursorImage(mwinAndroidPlatform* platform, mwinCursorId cursor);
void mwinAndroidReleaseCursor(mwinContext* context, uint32_t slot);

// Input methods, the keyboard and the insets (android_text.c): the Java
// activity's native methods registered and its methods found, at the
// start; the insets posted as they change and when the window is made;
// the text input and keyboard requests.
bool mwinAndroidFindText(mwinAndroidPlatform* platform, ANativeActivity* activity);
// A Java string as UTF-8 in a block from the allocator, which the caller
// releases with its size; null for an empty one or no memory. The
// UTF-16 offsets given are turned into byte offsets.
char* mwinAndroidBytesOf(const mwinAndroidPlatform* platform, JNIEnv* env, jstring text,
                         size_t* sizeOut, int32_t* offsets, size_t offsetCount);
void mwinAndroidPostInsets(mwinAndroidPlatform* platform);
mwinOutcome mwinAndroidSetTextInput(mwinAndroidPlatform* platform, bool enabled, mwinRect caret);
mwinOutcome mwinAndroidSetKeyboard(mwinAndroidPlatform* platform, bool visible,
                                   mwinInputPurpose purpose);

// The activity's display as the one monitor (android_output.c): the
// Java helper's methods found at the start and released at the stop;
// the display read when an activity starts and its configuration
// changes, and every two seconds after, as its HDR/SDR ratio changes
// with what it shows; the window told its monitor once it is made.
bool mwinAndroidFindScreen(mwinAndroidPlatform* platform, ANativeActivity* activity);
void mwinAndroidStopScreen(mwinAndroidPlatform* platform);
void mwinAndroidReadScreen(mwinAndroidPlatform* platform);
void mwinAndroidPumpScreen(mwinAndroidPlatform* platform, uint64_t nowNs);
void mwinAndroidPostDisplay(mwinAndroidPlatform* platform);

// The backend's window operations (android_window.c).
void mwinAndroidCreateWindow(mwinContext* context, uint32_t slot);
void mwinAndroidDestroyWindow(mwinContext* context, uint32_t slot);
void mwinAndroidSubmit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_ANDROID_H
