// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the Wayland backend keeps: the connection, the globals it bound,
// and per window slot and per output the objects and the state the
// compositor has told. One block from the context's allocator holds it
// all.

#ifndef MAUL_WINDOW_SRC_WAYLAND_H
#define MAUL_WINDOW_SRC_WAYLAND_H

#include "clicks.h"
#include "core.h"
#include "linux_pad.h"
#include "linux_services.h"
#include "monotonic.h"
#include "wayland_api.h"
#include "wayland_pipe.h"
#include "xkb_api.h"
#include "xkb_keyboard.h"

typedef struct mwinWaylandPlatform mwinWaylandPlatform;

// The parts of a window's own frame: the caption with its buttons above
// the content, and the invisible margins that resize it.
enum
{
    mwin_framePartCaption = 0,
    mwin_framePartTop = 1,
    mwin_framePartLeft = 2,
    mwin_framePartRight = 3,
    mwin_framePartBottom = 4,
    MWIN_FRAME_PARTS = 5,
};

// A frame the backend draws where the compositor draws none (W4): a
// subsurface per part, their buffers from one shared memory pool, and
// the caption button under the pointer.
typedef struct mwinWaylandFrame
{
    struct wl_surface* parts[MWIN_FRAME_PARTS];
    struct wl_subsurface* subsurfaces[MWIN_FRAME_PARTS];
    struct wl_buffer* buffers[MWIN_FRAME_PARTS];
    struct wl_shm_pool* pool;
    // The parts show, and the caption alone (maximized).
    bool shown;
    bool captionOnly;
    // The caption button under the pointer and the one pressed, or -1.
    int8_t hover;
    int8_t pressed;
    // The last press on the caption, for a double click.
    uint64_t captionPressNs;
} mwinWaylandFrame;

// A window's objects, and what the compositor proposed in the configure
// sequence that is still open.
typedef struct mwinWaylandWindow
{
    mwinWaylandPlatform* platform;
    uint32_t slot;
    struct wl_surface* surface;
    struct xdg_surface* xdgSurface;
    struct xdg_toplevel* toplevel;
    // A popup's role object, in place of the toplevel (wayland_popup.c):
    // the place it was last reported at against its owner's content, and
    // the token of its last reposition.
    struct xdg_popup* popup;
    mwinPosition placed;
    uint32_t repositions;
    struct zxdg_toplevel_decoration_v1* decoration;
    struct wp_fractional_scale_v1* fractionalScale;
    struct wp_viewport* viewport;
    // Keeps the display awake while the surface shows, while asked.
    struct zwp_idle_inhibitor_v1* inhibitor;
    // The token of a focus request, while it comes, and the request.
    struct xdg_activation_token_v1* activation;
    uint32_t activationRequest;
    // The proposal of the open configure sequence: a size of 0 leaves the
    // size to the window.
    int32_t proposedWidth;
    int32_t proposedHeight;
    // A popup's place from its owner's window geometry.
    int32_t proposedX;
    int32_t proposedY;
    mwinWindowMode proposedMode;
    bool proposedActivated;
    bool proposedSuspended;
    // The size the window has, in logical units, and its scale: in 120ths
    // from fractional scaling, or an integer buffer scale.
    mwinSize size;
    uint32_t scale120;
    int32_t bufferScale;
    // The first configure made the window (mwin_eventWindowCreated).
    bool configured;
    // Minimized on request. No configure says so, nor that the window
    // came back; its next activation does.
    bool minimized;
    // A mode request waits for the compositor's next configure, or -1.
    int32_t modeRequest;
    // The cursor the program asked for over the window, and the pointer
    // constraint its mode needs.
    mwinCursorMode cursorMode;
    mwinCursorShape cursorShape;
    struct zwp_locked_pointer_v1* locked;
    struct zwp_confined_pointer_v1* confined;
    // The window accepts text, and where its caret is.
    bool textInput;
    mwinRect caret;
    // The decoration mode the compositor chose, 0 before it says, and the
    // frame the backend draws otherwise.
    uint32_t decorationMode;
    mwinWaylandFrame frame;
} mwinWaylandWindow;

// An output the compositor announced, and the monitor slot it fills, or
// -1 before its first done event.
typedef struct mwinWaylandOutput
{
    mwinWaylandPlatform* platform;
    struct wl_output* output;
    uint32_t name;
    int32_t monitor;
    // Facts arrive one event at a time; done makes them true together.
    mwinMonitorInfo info;
    int32_t x;
    int32_t y;
    int32_t transform;
    int32_t scale;
} mwinWaylandOutput;

// The seat's keyboard: its xkb keyboard, the window with keyboard
// focus, and key repeat, which a Wayland client does itself.
typedef struct mwinWaylandKeyboard
{
    struct wl_keyboard* keyboard;
    mwinXkbKeyboard xkb;
    // The window slot with keyboard focus, or -1.
    int32_t focus;
    // Keys held since focus came, for the reset when it goes.
    uint32_t held;
    // Repeats per second (0 for none) and the delay before the first.
    int32_t repeatRate;
    int32_t repeatDelayMs;
    // The evdev code of the key that repeats, 0 for none, and when.
    uint32_t repeatKey;
    uint64_t repeatNextNs;
} mwinWaylandKeyboard;

// The seat's pointer: the window it is over, what a pointer frame has
// gathered so far, and the last press for counting quick clicks.
typedef struct mwinWaylandPointer
{
    struct wl_pointer* pointer;
    // The window slot under the pointer, or -1, and the serial of the
    // enter event, which cursor requests quote. The frame part under the
    // pointer instead, or NULL.
    int32_t focus;
    uint32_t enterSerial;
    struct wl_surface* framePart;
    mwinPosition position;
    uint8_t buttons;
    // Gathered until the frame event: motion, and per axis (0 vertical,
    // 1 horizontal) high-resolution steps (120ths of a detent), discrete
    // steps and continuous distance, of which the first present counts.
    bool moved;
    uint64_t timeNs;
    int32_t steps120[2];
    int32_t steps[2];
    double distance[2];
    uint8_t axisKinds[2];
    // The last press, for counting quick clicks.
    mwinClickCounter clicks;
    // The pointer's cursor shape device and relative motion, where the
    // compositor has them.
    struct wp_cursor_shape_device_v1* shapeDevice;
    struct zwp_relative_pointer_v1* relative;
} mwinWaylandPointer;

// Cursor images from the cursor theme, for a compositor without cursor
// shapes: the theme at the scale it was loaded for, and the surface the
// image is shown on.
typedef struct mwinWaylandCursorTheme
{
    struct wl_cursor_theme* theme;
    int32_t scale;
    struct wl_surface* surface;
} mwinWaylandCursorTheme;

// The most touches followed at once.
#define MWIN_WAYLAND_TOUCHES 16

// The seat's touch screen: each touch point by its id, with the window
// it began on.
typedef struct mwinWaylandTouch
{
    struct wl_touch* touch;
    struct
    {
        int32_t id;
        int32_t slot;
        mwinPosition position;
        bool active;
    } points[MWIN_WAYLAND_TOUCHES];
} mwinWaylandTouch;

// A string of the input method's, copied until the done event applies
// it; lost when it did not fit.
typedef struct mwinWaylandString
{
    char* bytes;
    uint32_t length;
    bool set;
    bool lost;
} mwinWaylandString;

// The seat's text input: the window it is focused on, and what the
// input method sent since its last done event.
typedef struct mwinWaylandText
{
    struct zwp_text_input_v3* textInput;
    int32_t focus;
    mwinWaylandString preedit;
    int32_t cursorBegin;
    int32_t cursorEnd;
    mwinWaylandString commit;
} mwinWaylandText;

// The most readers served the program's text at once.
#define MWIN_WAYLAND_SENDS 4

// The clipboard: the data device, the selection another client offers
// and the text type it has, the source while the program owns the
// selection with the pipes of its readers, and a read under way.
typedef struct mwinWaylandClipboard
{
    struct wl_data_device_manager* manager;
    struct wl_data_device* device;
    // The newest offer and its best text type, an index of the types the
    // backend knows or -1, until the selection names it.
    struct wl_data_offer* incoming;
    int8_t incomingType;
    // The newest offer has files (text/uri-list).
    bool incomingFiles;
    struct wl_data_offer* selection;
    int8_t selectionType;
    struct wl_data_source* source;
    struct
    {
        int fd;
        uint32_t offset;
    } sends[MWIN_WAYLAND_SENDS];
    // The read's pipe, and when it fails.
    mwinWaylandPipe reading;
    uint64_t deadlineNs;
} mwinWaylandClipboard;

// A drag over one of the program's windows: its offer, the window, the
// enter's serial, what it carries and the type its text comes in, and
// where it is. A drop takes the offer on to be read, its files and text
// through pipes of their own, until a deadline.
typedef struct mwinWaylandDrag
{
    struct wl_data_offer* offer;
    int32_t slot;
    uint32_t serial;
    mwinDragContents contents;
    const char* textType;
    mwinPosition position;
    struct wl_data_offer* dropped;
    int32_t dropSlot;
    mwinPosition dropPosition;
    mwinWaylandPipe files;
    mwinWaylandPipe text;
    uint64_t deadlineNs;
    // A pipe for the drop could not be made.
    bool lost;
} mwinWaylandDrag;

struct mwinWaylandPlatform
{
    mwinWaylandApi api;
    mwinContext* context;
    // The gamepads, with the gamepad component.
    mwinLinuxPads pads;
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_compositor* compositor;
    struct xdg_wm_base* wmBase;
    struct zxdg_decoration_manager_v1* decorations;
    struct wp_fractional_scale_manager_v1* fractionalScale;
    struct wp_viewporter* viewporter;
    // Without it, keeping awake goes over the session bus.
    struct zwp_idle_inhibit_manager_v1* idleInhibits;
    // Without it, windows have the compositor's icons.
    struct xdg_toplevel_icon_manager_v1* toplevelIcons;
    struct wp_cursor_shape_manager_v1* cursorShapes;
    struct zwp_pointer_constraints_v1* constraints;
    struct zwp_relative_pointer_manager_v1* relativePointers;
    struct wl_shm* shm;
    struct wl_subcompositor* subcompositor;
    struct zwp_text_input_manager_v3* textInputs;
    // Without it, focus requests are unsupported.
    struct xdg_activation_v1* activation;
    mwinWaylandCursorTheme cursorTheme;
    // The first seat, its registry name, and its keyboard. libxkbcommon
    // loads with the context; without it there is no keyboard.
    struct wl_seat* seat;
    uint32_t seatName;
    mwinXkbApi xkb;
    mwinWaylandKeyboard keyboard;
    mwinWaylandPointer pointer;
    mwinWaylandTouch touch;
    mwinWaylandText text;
    mwinWaylandClipboard clipboard;
    mwinWaylandDrag drag;
    // Addresses, files and the bus (linux_services.c).
    mwinLinuxServices services;
    // The serial of the latest input event, which setting the selection
    // quotes.
    uint32_t inputSerial;
    // The connection failed; the loop stops.
    bool failed;
    // One per window slot, and one per monitor slot.
    mwinWaylandWindow* windows;
    mwinWaylandOutput* outputs;
    // A NUL-terminated copy of a title, titleBytes + 1 bytes.
    char* title;
};

// The integer scale an image needs over a window: its scale, rounded
// up. Cursor images and the frame are drawn at it.
// Shared memory of a size, mapped for writing: its descriptor, or -1
// (wayland_frame.c).
int mwinWaylandMapMemory(size_t bytes, void** memory);

static inline int32_t mwinWaylandImageScale(const mwinWaylandWindow* window)
{
    return window->scale120 != 0 ? (int32_t)((window->scale120 + 119u) / 120u)
                                 : window->bufferScale;
}

// The slot of the window whose surface this is, or -1.
static inline int32_t mwinWaylandSlotOf(const mwinWaylandPlatform* platform,
                                        const struct wl_surface* surface)
{
    for (uint32_t i = 0; surface != nullptr && i < platform->context->limits.windows; i++)
    {
        if (platform->windows[i].surface == surface)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

#endif // MAUL_WINDOW_SRC_WAYLAND_H
