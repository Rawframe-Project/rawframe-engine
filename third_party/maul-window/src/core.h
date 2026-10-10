// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context and what it owns, and the functions backends call to
// report what the platform did. Everything lives in one block from the
// def's allocator: the window slots, each with its record storage, its
// request slots and its two title buffers.
//
// A window slot is free, live, or destroyed with its
// mwin_eventWindowDestroyed record still waiting; it is free again once
// that record is drained, so a burst of destroyed windows can never
// outgrow the storage.

#ifndef MAUL_WINDOW_SRC_CORE_H
#define MAUL_WINDOW_SRC_CORE_H

#include "backend.h"
#include "file_list.h"

#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/monitor.h"
#include "maul-window/system.h"

// The classes of records, each with its own storage per window.
enum
{
    mwin_classNotification = 0,
    mwin_classDiscrete = 1,
    mwin_classMotion = 2,
    mwin_classRaw = 3,
    mwin_classWheel = 4,
    MWIN_CLASSES = 5,
};

// Text of waiting records, in a circular buffer of bytes. A text never
// wraps, so the end of the buffer may be skipped; busy counts the bytes
// from head to tail, skipped ones included. Drained text is reclaimed
// only when the next pump begins (reclaimHead and reclaimBytes say how
// far), so it stays valid through the frame that drained it.
typedef struct mwinTextRing
{
    char* bytes;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t busy;
    uint32_t reclaimHead;
    uint32_t reclaimBytes;
} mwinTextRing;

// Records waiting in arrival order, in a circular buffer.
typedef struct mwinRing
{
    mwinEvent* events;
    uint64_t* sequences;
    uint32_t head;
    uint32_t count;
    uint32_t capacity;
} mwinRing;

// A request slot is free, active (the backend has it), or answered (its
// completion waits in the stream); it is free again once the completion
// is drained, so completions waiting never outnumber the slots.
enum
{
    mwin_requestFree = 0,
    mwin_requestActive = 1,
    mwin_requestAnswered = 2,
};

typedef struct mwinRequest
{
    uint32_t generation;
    uint8_t status;
    mwinRequestKind kind;
    union
    {
        mwinSize size;
        mwinPosition position;
        mwinWindowMode mode;
        bool visible;
        // A cursor mode or shape.
        uint8_t code;
        struct
        {
            bool enabled;
            mwinRect caret;
        } textInput;
        struct
        {
            mwinSize minimum;
            mwinSize maximum;
        } limits;
        struct
        {
            uint32_t width;
            uint32_t height;
        } aspect;
        float opacity;
        // An address or path: a copy with a NUL after it, in a block of
        // length + 1 bytes from the allocator, given back when the request
        // is answered.
        struct
        {
            char* bytes;
            uint32_t length;
        } text;
        bool awake;
        // A file dialog's def, given back when the request is answered
        // (dialog.h).
        struct mwinDialogCopy* dialog;
        // An icon's images, given back when the request is answered
        // (icon.h).
        struct mwinIconCopy* icon;
        // An accessibility root (accessibility.h).
        void* root;
        // A cursor made from images (cursor.h).
        mwinCursorId cursor;
    } value;
} mwinRequest;

enum
{
    mwin_slotFree = 0,
    mwin_slotLive = 1,
    mwin_slotDestroyed = 2,
};

// The payloads clipboard reads find, by kind: the clipboard's text, its
// data of a type, the primary selection's text.
enum
{
    mwin_foundText = 0,
    mwin_foundData = 1,
    mwin_foundPrimary = 2,
    mwin_foundKinds = 3,
};

// A window's last read of a kind that completed done: its request and
// the number of the payload that answered it.
typedef struct mwinFoundRead
{
    mwinRequestId request;
    uint32_t payload;
} mwinFoundRead;

typedef struct mwinWindow
{
    uint32_t generation;
    uint8_t status;
    mwinWindowState state;
    // What the program asked for at creation, for the backend.
    mwinWindowDef def;
    mwinRing rings[MWIN_CLASSES];
    mwinTextRing text;
    // One bit per key code whose press a composition consumed, so its
    // release goes too.
    uint8_t consumedKeys[32];
    mwinRequest* requests;
    char* title;
    char* pendingTitle;
    uint16_t titleLength;
    uint16_t pendingTitleLength;
    // The hit regions the program declared (chrome.c).
    mwinHitRegion regions[MWIN_HIT_REGIONS];
    uint8_t regionCount;
    // The accessibility root the platform took, and whether a client has
    // asked for the tree (accessibility.c).
    void* accessibilityRoot;
    bool accessibilityAsked;
    // Its reads answered done, by kind (clipboard_data.c).
    mwinFoundRead foundReads[mwin_foundKinds];
    // The backend's own data for the window.
    void* platform;
} mwinWindow;

typedef struct mwinMonitor
{
    uint32_t generation;
    uint8_t status;
    mwinMonitorInfo info;
} mwinMonitor;

typedef struct mwinGamepad
{
    uint32_t generation;
    uint8_t status;
    // The order it came in, for listing.
    uint64_t arrival;
    mwinGamepadInfo info;
    mwinGamepadState state;
    // Its motion sensors are on, and their motion since the last read.
    bool motionOn;
    mwinGamepadMotion motion;
} mwinGamepad;

// The rings of the gamepads' records: buttons, and axes.
enum
{
    mwin_padRingButtons = 0,
    mwin_padRingAxes = 1,
};

// A drop's files, each path ended by a NUL, and its text, each in a
// block of its capacity from the allocator.
typedef struct mwinDropPayload
{
    mwinFileList files;
    char* text;
    uint32_t textLength;
    bool truncated;
} mwinDropPayload;

struct mwinContext
{
    mwinAllocator allocator;
    size_t memorySize;
    // Calls refused as invalid input, counted through misuse, which
    // points at misuseCount so that calls taking a const context count
    // too: a context is never a const object.
    uint64_t misuseCount;
    uint64_t* misuse;
    mwinLimits limits;
    const mwinBackendOps* backend;
    void* backendData;
    // What the platform handed the backend's start where it, not the
    // program, begins the run (Android's activity); null from mwinRun.
    void* launch;
    mwinAppDef app;
    uint64_t sequence;
    mwinWindow* windows;
    // Slots like the windows': free, live, or removed with its record
    // still waiting.
    mwinMonitor* monitors;
    // One per gamepad slot, the rings of their records, and the arrivals
    // counted.
    mwinGamepad* gamepads;
    // One per cursor slot (cursor.h).
    struct mwinCursor* cursors;
    mwinRing gamepadRings[2];
    uint64_t arrivals;
    mwinSystemFacts facts;
    char* locales;
    uint16_t localeLength;
    // The text last written to the clipboard and the text the last read
    // found, each in a block of its own length from the allocator; the
    // data last written and the data the last data read found; the
    // primary selection's text written and found, likewise.
    char* clipboardOffer;
    uint32_t clipboardOfferLength;
    char* clipboardFound;
    uint32_t clipboardFoundLength;
    struct mwinClipboardCopy* clipboardData;
    uint8_t* clipboardDataFound;
    uint32_t clipboardDataFoundLength;
    char* primaryOffer;
    uint32_t primaryOfferLength;
    char* primaryFound;
    uint32_t primaryFoundLength;
    // How many payloads reads of each kind have found; a payload is known
    // by the count it made, and a read's getter answers only while its
    // payload is the last.
    uint32_t foundPayloads[mwin_foundKinds];
    // The drop being gathered, and the last one delivered with its
    // number.
    mwinDropPayload dropping;
    mwinDropPayload dropped;
    uint32_t dropNumber;
    // The paths of the dialog being answered and how it goes, and those
    // of the last dialog done, with its request.
    mwinFileList dialogGathering;
    mwinOutcome dialogAnswer;
    mwinFileList dialogFiles;
    mwinRequestId dialogRequest;
    // Lifecycle and surface notifications, which come before all others,
    // and the context's own notifications.
    mwinRing critical;
    mwinRing global;
    // Between init's success and quit.
    bool running;
    // A frame asked to stop.
    bool stopping;
    // The program's functions are running; a critical frame waits.
    bool inProgram;
    // The platform's loop runs the program's frames after the backend's
    // run returned; its last frame ends the program and frees the
    // context (mwin-0022).
    bool loopOutlivesRun;
    // What init returned.
    mwinResult status;
};

// The window a live id names, or NULL.
mwinWindow* mwinFindWindow(const mwinContext* context, mwinWindowId window);

// The id of the window in a slot.
mwinWindowId mwinWindowIdOf(const mwinContext* context, uint32_t slot);

// Reports what the platform did to the window in a slot. A notification
// updates the window's state and replaces a waiting one of its class;
// input takes its class's storage, merging or resetting when it is full
// (see event.h). Text is copied; text that is not UTF-8 is refused.
void mwinPost(mwinContext* context, uint32_t slot, const mwinEvent* event);

// Reports a notification about the application rather than a window:
// the lifecycle, and later monitors and system facts.
void mwinPostGlobal(mwinContext* context, const mwinEvent* event);

// A monitor was connected: its slot, or -1 when the context has its
// limit of monitors and leaves it out.
int32_t mwinAddMonitor(mwinContext* context, const mwinMonitorInfo* info, uint64_t timeNs);

// A connected monitor's facts changed.
void mwinChangeMonitor(mwinContext* context, uint32_t slot, const mwinMonitorInfo* info,
                       uint64_t timeNs);

// Whether two reads of a monitor tell the same facts, so a backend
// reports a change only when one is.
bool mwinSameMonitorInfo(const mwinMonitorInfo* a, const mwinMonitorInfo* b);

// A monitor was disconnected; its slot is free once the record is
// drained.
void mwinRemoveMonitor(mwinContext* context, uint32_t slot, uint64_t timeNs);

// The system's facts as the platform reports them; changes post their
// notifications.
void mwinSetSystemFacts(mwinContext* context, const mwinSystemFacts* facts, uint64_t timeNs);

// The preferred locales as the platform reports them; false for a list
// that is not UTF-8 or past the localeBytes limit.
bool mwinSetLocales(mwinContext* context, const char* locales, size_t length, uint64_t timeNs);

// Gamepads as the backend reports them: a new one's slot (-1 when every
// slot is taken), a change of its facts, its removal; its buttons and
// axes, posted only when they change. The slot of a removed gamepad is
// freed once its record is drained.
int32_t mwinAddGamepad(mwinContext* context, const mwinGamepadInfo* info, uint64_t timeNs);
void mwinChangeGamepad(mwinContext* context, uint32_t slot, const mwinGamepadInfo* info,
                       uint64_t timeNs);
void mwinRemoveGamepad(mwinContext* context, uint32_t slot, uint64_t timeNs);
void mwinPostGamepadButton(mwinContext* context, uint32_t slot, uint8_t button, bool down,
                           uint64_t timeNs);
void mwinPostGamepadAxis(mwinContext* context, uint32_t slot, uint8_t axis, float value,
                         uint64_t timeNs);
// A sample of a gamepad's motion sensors, kept while they are on, in
// the contract's units and frame, at a time on any steady clock the
// backend keeps for the gamepad.
void mwinPostGamepadMotion(mwinContext* context, uint32_t slot, const float acceleration[3],
                           const float rotationRate[3], uint64_t timeNs);
mwinGamepadId mwinGamepadIdOf(const mwinContext* context, uint32_t slot);
int32_t mwinFindGamepad(const mwinContext* context, mwinGamepadId gamepad);
void mwinReleaseGamepad(mwinContext* context, mwinGamepadId gamepad);

// Queues a gamepad's record: a button's never merges, and when its ring
// is full the gamepad gets a reset; an axis's merges into the newest of
// its axis waiting.
void mwinPostGamepadRecord(mwinContext* context, const mwinEvent* event);

// The id of the monitor in a slot, and the slot of a live id or -1.
mwinMonitorId mwinMonitorIdOf(const mwinContext* context, uint32_t slot);
int32_t mwinFindMonitor(const mwinContext* context, mwinMonitorId monitor);

// Frees a removed monitor's slot once its record is drained.
void mwinReleaseMonitor(mwinContext* context, mwinMonitorId monitor);

// Runs a frame now, from inside a platform callback that waits for the
// program to handle a critical notification; does nothing while the
// program's functions run or before init has succeeded.
void mwinRunCriticalFrame(mwinContext* context);

// Reclaims the text of records drained before; a backend calls it when
// a pump begins.
void mwinBeginPump(mwinContext* context);

// Queues the destroyed record of a slot whose window just went; its
// notifications still waiting go with it, its completions stay.
void mwinPostDestroyed(mwinContext* context, uint32_t slot, uint64_t timeNs);

// Answers a request of the window in a slot; a request that is no
// longer active (superseded, cancelled) is left alone.
void mwinComplete(mwinContext* context, uint32_t slot, uint32_t request, mwinOutcome outcome);

// Holds what a clipboard read found, the platform's bytes as UTF-8 or
// UTF-16: ill-formed sequences replaced, then checked against the
// clipboardBytes limit. The outcome to complete the read with: done,
// too large, or failed when the allocator has no room.
mwinOutcome mwinTakeClipboardText(mwinContext* context, const char* bytes, size_t length);
mwinOutcome mwinTakeClipboardUtf16(mwinContext* context, const uint16_t* units, size_t length);

// Holds what a data read or a primary selection read found, as
// mwinTakeClipboardText does (clipboard_data.c): data as it is, the
// primary selection's text repaired.
mwinOutcome mwinTakeClipboardData(mwinContext* context, const void* bytes, size_t length);
mwinOutcome mwinTakePrimaryText(mwinContext* context, const char* bytes, size_t length);

// Frees the clipboard's text; the context's end calls it.
void mwinReleaseClipboard(mwinContext* context);

// A drop as the backend gathers it: begun, its files and text added as
// the platform gives them (paths that are not UTF-8 and what passes the
// limits left out, text repaired), then delivered to a window with its
// record, replacing the last drop.
void mwinBeginDrop(mwinContext* context);
void mwinAddDroppedFile(mwinContext* context, const char* path, size_t length);
void mwinAddDroppedFileUtf16(mwinContext* context, const uint16_t* path, size_t length);
void mwinSetDroppedText(mwinContext* context, const char* bytes, size_t length);
void mwinSetDroppedTextUtf16(mwinContext* context, const uint16_t* units, size_t length);
void mwinFinishDrop(mwinContext* context, uint32_t slot, mwinPosition position, uint64_t timeNs);

// Gives back the text an address or path request holds, if it does;
// answering a request, and the context's end, call it.
void mwinReleaseRequestText(const mwinContext* context, mwinRequest* request);

// Gives back the def a file dialog's request holds, if it does.
void mwinReleaseDialogCopy(const mwinContext* context, mwinRequest* request);

// Gives back the images an icon's request holds, if it does.
void mwinReleaseIconCopy(const mwinContext* context, mwinRequest* request);

// Gives back whatever a request holds: its text, def or images.
void mwinReleaseRequestData(const mwinContext* context, mwinRequest* request);

// Counts one misuse on a live context and answers mwin_errorInvalid,
// which a refused call returns; a NULL context counts nothing. Inline,
// so that analysis sees every refusal fail.
static inline mwinResult mwinMisuse(const mwinContext* context)
{
    if (context != nullptr)
    {
        ++*context->misuse;
    }
    return mwin_errorInvalid;
}

// The id of the request in a window slot's request slot.
mwinRequestId mwinRequestIdOf(const mwinContext* context, uint32_t slot, uint32_t request);

// Whether some window asked to keep the display awake and shows: live,
// visible and not minimized. Backends keep the display awake while it
// holds.
bool mwinWantsAwake(const mwinContext* context);

// Frees the drops' files and text; the context's end calls it.
void mwinReleaseDrops(mwinContext* context);

// Gives back the paths dialogs hold, at the context's end.
void mwinReleaseDialogs(mwinContext* context);

// Frees the slot of an answered request whose completion was drained.
void mwinReleaseRequest(mwinContext* context, mwinRequestId request);

// Starts a request: checks the context and the window and takes a
// request slot, superseding an active request of the kind. The caller
// then sets the request's value and calls mwinSubmitRequest.
mwinResult mwinBeginRequest(mwinContext* context, mwinWindowId window, mwinRequestKind kind,
                            uint32_t* slotOut, int32_t* requestOut);

// Hands a started request to the backend and reports its id.
void mwinSubmitRequest(mwinContext* context, uint32_t slot, int32_t request,
                       mwinRequestId* requestOut);

// The request slot of an active request of a kind, or -1.
int32_t mwinFindActiveRequest(const mwinWindow* window, uint16_t count, mwinRequestKind kind);

#endif // MAUL_WINDOW_SRC_CORE_H
