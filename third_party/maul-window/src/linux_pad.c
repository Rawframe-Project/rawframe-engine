// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Linux gamepads.

#include "linux_pad.h"

#include "allocator.h"
#include "monotonic.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdckdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define LONG_BITS   (sizeof(unsigned long) * CHAR_BIT)
#define LONGS(bits) (((bits) + LONG_BITS - 1) / LONG_BITS)

// What a device says it has.
typedef struct Bits
{
    unsigned long keys[LONGS(KEY_CNT)];
    unsigned long axes[LONGS(ABS_CNT)];
    unsigned long props[LONGS(INPUT_PROP_CNT)];
    unsigned long effects[LONGS(FF_CNT)];
} Bits;

static bool Has(const unsigned long* bits, unsigned bit)
{
    return ((bits[bit / LONG_BITS] >> (bit % LONG_BITS)) & 1u) != 0;
}

static bool HasAny(const unsigned long* bits, unsigned first, unsigned count)
{
    for (unsigned i = first; i < first + count; i++)
    {
        if (Has(bits, i))
        {
            return true;
        }
    }
    return false;
}

static void ReadBits(int fd, Bits* bits)
{
    memset(bits, 0, sizeof(*bits));
    (void)ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits->keys)), bits->keys);
    (void)ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits->axes)), bits->axes);
    (void)ioctl(fd, EVIOCGPROP(sizeof(bits->props)), bits->props);
    (void)ioctl(fd, EVIOCGBIT(EV_FF, sizeof(bits->effects)), bits->effects);
}

// A gamepad or joystick, not a keyboard, mouse, tablet or a pad's
// motion sensors.
static bool IsGamepad(const Bits* bits)
{
    bool controls = Has(bits->keys, BTN_GAMEPAD) ||
                    HasAny(bits->keys, BTN_JOYSTICK, BTN_GAMEPAD - BTN_JOYSTICK) ||
                    HasAny(bits->keys, BTN_TRIGGER_HAPPY, 40);
    bool other = Has(bits->keys, BTN_TOUCH) || Has(bits->keys, BTN_TOOL_PEN) ||
                 Has(bits->keys, BTN_LEFT) || Has(bits->props, INPUT_PROP_ACCELEROMETER);
    return controls && !other;
}

// Numbers the buttons, axes and hats as SDL does: buttons from
// BTN_JOYSTICK up, then those below it; axes in code order, hats apart.
static void Number(mwinLinuxPad* pad, int fd, const Bits* bits)
{
    pad->controls.buttonCount = 0;
    for (unsigned pass = 0; pass < 2; pass++)
    {
        unsigned first = pass == 0 ? BTN_JOYSTICK : 0;
        unsigned last = pass == 0 ? KEY_MAX : BTN_JOYSTICK;
        for (unsigned code = first;
             code < last && pad->controls.buttonCount < MWIN_LINUX_PAD_BUTTONS; code++)
        {
            if (Has(bits->keys, code))
            {
                pad->buttonCodes[pad->controls.buttonCount++] = (uint16_t)code;
            }
        }
    }
    memset(pad->axisOf, 0, sizeof(pad->axisOf));
    pad->controls.axisCount = 0;
    for (unsigned code = 0; code < ABS_HAT0X && pad->controls.axisCount < MWIN_LINUX_PAD_AXES;
         code++)
    {
        struct input_absinfo info;
        if (Has(bits->axes, code) && ioctl(fd, EVIOCGABS(code), &info) == 0)
        {
            pad->minimum[pad->controls.axisCount] = info.minimum;
            pad->maximum[pad->controls.axisCount] = info.maximum;
            pad->axisOf[code] = ++pad->controls.axisCount;
        }
    }
    pad->controls.hatCount = 0;
    for (unsigned hat = 0; hat < MWIN_LINUX_PAD_HATS; hat++)
    {
        bool present = Has(bits->axes, ABS_HAT0X + 2 * hat) || Has(bits->axes, ABS_HAT0Y + 2 * hat);
        pad->hatNumbers[hat] = present ? ++pad->controls.hatCount : 0;
    }
}

static mwinPadSource ButtonSource(const mwinLinuxPad* pad, unsigned code)
{
    for (uint8_t i = 0; i < pad->controls.buttonCount; i++)
    {
        if (pad->buttonCodes[i] == code)
        {
            return (mwinPadSource)(mwin_padSourceButton << 14 | i);
        }
    }
    return 0;
}

static mwinPadSource AxisSource(const mwinLinuxPad* pad, unsigned code)
{
    return pad->axisOf[code] != 0
               ? (mwinPadSource)(mwin_padSourceAxis << 14 | (pad->axisOf[code] - 1))
               : 0;
}

// A mapping from the kernel's gamepad layout, for a device that follows
// it and that the database lacks.
static void MakeKernelMapping(mwinLinuxPad* pad)
{
    static const uint16_t buttons[] = {BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
                                       BTN_SOUTH,   BTN_EAST,      BTN_WEST,      BTN_NORTH,
                                       BTN_TL,      BTN_TR,        BTN_THUMBL,    BTN_THUMBR,
                                       BTN_START,   BTN_SELECT,    BTN_MODE};
    static const uint16_t axes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
    static const uint8_t directions[] = {1, 4, 8, 2};
    mwinPadMapping* own = &pad->own;
    *own = (mwinPadMapping){0};
    for (unsigned i = 0; i < MWIN_GAMEPAD_BUTTONS; i++)
    {
        own->sources[i] = ButtonSource(pad, buttons[i]);
        if (i < 4 && own->sources[i] == 0 && pad->hatNumbers[0] != 0)
        {
            own->sources[i] = (mwinPadSource)(mwin_padSourceHat << 14 |
                                              (pad->hatNumbers[0] - 1) << 4 | directions[i]);
        }
    }
    for (unsigned i = 0; i < MWIN_GAMEPAD_AXES; i++)
    {
        own->sources[MWIN_GAMEPAD_BUTTONS + i] = AxisSource(pad, axes[i]);
    }
    // Triggers that are only buttons.
    for (unsigned i = 0; i < 2; i++)
    {
        mwinPadSource* trigger = &own->sources[MWIN_GAMEPAD_BUTTONS + mwin_padTriggerLeft + i];
        *trigger = *trigger != 0 ? *trigger : ButtonSource(pad, i == 0 ? BTN_TL2 : BTN_TR2);
    }
    pad->controls.mapping = own;
    pad->controls.halves = nullptr;
}

// Takes an absolute axis's new value: a hat's direction, or an axis.
static void OnAxis(mwinLinuxPad* pad, unsigned code, int32_t value)
{
    if (code >= ABS_HAT0X && code <= ABS_HAT3Y)
    {
        unsigned hat = (code - ABS_HAT0X) / 2;
        if (pad->hatNumbers[hat] == 0)
        {
            return;
        }
        uint8_t* bits = &pad->controls.hats[pad->hatNumbers[hat] - 1];
        bool vertical = ((code - ABS_HAT0X) & 1u) != 0;
        uint8_t negative = vertical ? 1 : 8;
        uint8_t positive = vertical ? 4 : 2;
        *bits = (uint8_t)(*bits & ~(negative | positive));
        *bits |= value < 0 ? negative : (value > 0 ? positive : 0);
        return;
    }
    if (code < sizeof(pad->axisOf) && pad->axisOf[code] != 0)
    {
        uint8_t axis = (uint8_t)(pad->axisOf[code] - 1);
        pad->controls.axes[axis] = mwinPadNormalize(value, pad->minimum[axis], pad->maximum[axis]);
    }
}

static void OnKey(mwinLinuxPad* pad, unsigned code, int32_t value)
{
    for (uint8_t i = 0; i < pad->controls.buttonCount; i++)
    {
        if (pad->buttonCodes[i] == code)
        {
            pad->controls.buttons[i] = value != 0;
            return;
        }
    }
}

// Reads the device's whole state, as after events were dropped.
static void Resync(mwinLinuxPad* pad)
{
    unsigned long keys[LONGS(KEY_CNT)] = {0};
    (void)ioctl(pad->fd, EVIOCGKEY(sizeof(keys)), keys);
    for (uint8_t i = 0; i < pad->controls.buttonCount; i++)
    {
        pad->controls.buttons[i] = Has(keys, pad->buttonCodes[i]);
    }
    for (unsigned code = 0; code <= ABS_HAT3Y; code++)
    {
        struct input_absinfo info;
        bool known = code >= ABS_HAT0X ? pad->hatNumbers[(code - ABS_HAT0X) / 2] != 0
                                       : code < sizeof(pad->axisOf) && pad->axisOf[code] != 0;
        if (known && ioctl(pad->fd, EVIOCGABS(code), &info) == 0)
        {
            OnAxis(pad, code, info.value);
        }
    }
}

// Opens /dev/input/eventN, and makes it a gamepad if it is one.
static void Open(mwinLinuxPads* pads, int node)
{
    mwinLinuxPad* pad = nullptr;
    for (uint32_t i = 0; i < pads->context->limits.gamepads; i++)
    {
        if (pads->pads[i].fd >= 0 && pads->pads[i].node == node)
        {
            return;
        }
        pad = pad == nullptr && pads->pads[i].fd < 0 ? &pads->pads[i] : pad;
    }
    char path[32];
    (void)snprintf(path, sizeof(path), "/dev/input/event%d", node);
    // Writing is for rumble; a device that is only readable has none.
    bool writable = true;
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
    {
        writable = false;
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (fd < 0)
    {
        return;
    }
    Bits bits;
    ReadBits(fd, &bits);
    if (pad == nullptr || !IsGamepad(&bits))
    {
        close(fd);
        return;
    }
    *pad = (mwinLinuxPad){.fd = fd, .node = node, .effect = -1};
    // Event times on the clock the records use.
    int clock = CLOCK_MONOTONIC;
    (void)ioctl(fd, EVIOCSCLOCKID, &clock);
    Number(pad, fd, &bits);
    struct input_id id = {0};
    (void)ioctl(fd, EVIOCGID, &id);
    mwinGamepadInfo info = {.vendor = id.vendor, .product = id.product, .battery = -1};
    int named = ioctl(fd, EVIOCGNAME(sizeof(info.name)), info.name);
    info.nameLength = named > 0 ? (uint32_t)strnlen(info.name, sizeof(info.name)) : 0;
    pad->controls.mapping =
        mwinFindPadMapping(&mwinLinuxPadDatabase, id.bustype, id.vendor, id.product, id.version);
    uint8_t halves = pad->controls.mapping != nullptr ? pad->controls.mapping->halves : 0;
    pad->controls.halves = halves != 0 ? mwinLinuxPadDatabase.halves[halves - 1] : nullptr;
    if (pad->controls.mapping == nullptr && Has(bits.keys, BTN_GAMEPAD))
    {
        MakeKernelMapping(pad);
    }
    info.mapped = pad->controls.mapping != nullptr;
    info.rawButtons = pad->controls.buttonCount;
    info.rawAxes = (uint8_t)(pad->controls.axisCount + 2 * pad->controls.hatCount);
    info.capabilities = writable && Has(bits.effects, FF_RUMBLE) ? mwin_padRumble : 0;
    int32_t slot = mwinAddGamepad(pads->context, &info, mwinMonotonicNow());
    if (slot < 0)
    {
        close(fd);
        pad->fd = -1;
        return;
    }
    pad->slot = (uint32_t)slot;
    Resync(pad);
    mwinPostPadControls(pads->context, pad->slot, &pad->controls, mwinMonotonicNow());
}

static void Close(mwinLinuxPads* pads, mwinLinuxPad* pad)
{
    mwinRemoveGamepad(pads->context, pad->slot, mwinMonotonicNow());
    close(pad->fd);
    pad->fd = -1;
}

// The N of a node's name "eventN", or -1.
static int NodeOf(const char* name)
{
    int node = -1;
    char end = 0;
    return sscanf(name, "event%d%c", &node, &end) == 1 && node >= 0 ? node : -1;
}

// Reads a device's events: false when it is gone.
static bool Read(mwinLinuxPads* pads, mwinLinuxPad* pad)
{
    struct input_event events[32];
    for (;;)
    {
        ssize_t bytes = read(pad->fd, events, sizeof(events));
        if (bytes < 0)
        {
            return errno == EAGAIN || errno == EINTR;
        }
        for (size_t i = 0; i < (size_t)bytes / sizeof(events[0]); i++)
        {
            const struct input_event* event = &events[i];
            uint64_t timeNs = (uint64_t)event->input_event_sec * 1000000000u +
                              (uint64_t)event->input_event_usec * 1000u;
            if (event->type == EV_KEY)
            {
                OnKey(pad, event->code, event->value);
            }
            else if (event->type == EV_ABS)
            {
                OnAxis(pad, event->code, event->value);
            }
            else if (event->type == EV_SYN && event->code == SYN_DROPPED)
            {
                Resync(pad);
            }
            else if (event->type == EV_SYN && event->code == SYN_REPORT)
            {
                mwinPostPadControls(pads->context, pad->slot, &pad->controls, timeNs);
            }
        }
    }
}

// Takes what inotify says about /dev/input.
static void Watch(mwinLinuxPads* pads)
{
    union
    {
        struct inotify_event event;
        char bytes[4096];
    } buffer;
    ssize_t length;
    while ((length = read(pads->watch, buffer.bytes, sizeof(buffer.bytes))) > 0)
    {
        for (ssize_t at = 0; at < length;)
        {
            const struct inotify_event* event = (const struct inotify_event*)(buffer.bytes + at);
            int node = event->len > 0 ? NodeOf(event->name) : -1;
            if (node >= 0 && (event->mask & (IN_CREATE | IN_ATTRIB | IN_MOVED_TO)) != 0)
            {
                Open(pads, node);
            }
            at += (ssize_t)(sizeof(struct inotify_event) + event->len);
        }
    }
}

bool mwinLinuxPadsStart(mwinLinuxPads* pads, mwinContext* context)
{
    *pads = (mwinLinuxPads){.context = context, .watch = -1};
    uint32_t count = context->limits.gamepads;
    if (count == 0)
    {
        return true;
    }
    size_t bytes = 0;
    pads->pads = ckd_mul(&bytes, (size_t)count, sizeof(mwinLinuxPad))
                     ? nullptr
                     : mwinAllocate(&context->allocator, bytes, alignof(mwinLinuxPad));
    if (pads->pads == nullptr)
    {
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        pads->pads[i].fd = -1;
    }
    // Watching first, so a device that comes during the scan is not
    // missed.
    pads->watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (pads->watch >= 0 &&
        inotify_add_watch(pads->watch, "/dev/input", IN_CREATE | IN_ATTRIB | IN_MOVED_TO) < 0)
    {
        close(pads->watch);
        pads->watch = -1;
    }
    DIR* directory = opendir("/dev/input");
    for (struct dirent* entry = directory != nullptr ? readdir(directory) : nullptr;
         entry != nullptr; entry = readdir(directory))
    {
        int node = NodeOf(entry->d_name);
        if (node >= 0)
        {
            Open(pads, node);
        }
    }
    if (directory != nullptr)
    {
        closedir(directory);
    }
    return true;
}

void mwinLinuxPadsStop(mwinLinuxPads* pads)
{
    mwinContext* context = pads->context;
    if (context == nullptr)
    {
        return; // never started
    }
    for (uint32_t i = 0; pads->pads != nullptr && i < context->limits.gamepads; i++)
    {
        if (pads->pads[i].fd >= 0)
        {
            close(pads->pads[i].fd);
        }
    }
    if (pads->pads != nullptr)
    {
        mwinRelease(&context->allocator, pads->pads,
                    context->limits.gamepads * sizeof(mwinLinuxPad), alignof(mwinLinuxPad));
    }
    if (pads->watch >= 0)
    {
        close(pads->watch);
    }
    *pads = (mwinLinuxPads){.watch = -1};
}

void mwinLinuxPadsPump(mwinLinuxPads* pads)
{
    if (pads->pads == nullptr)
    {
        return;
    }
    if (pads->watch >= 0)
    {
        Watch(pads);
    }
    // A device that is gone reads as such; its node's removal needs no
    // watching.
    for (uint32_t i = 0; i < pads->context->limits.gamepads; i++)
    {
        mwinLinuxPad* pad = &pads->pads[i];
        if (pad->fd >= 0 && !Read(pads, pad))
        {
            Close(pads, pad);
        }
    }
}

mwinResult mwinLinuxPadsRumble(mwinLinuxPads* pads, uint32_t slot, float low, float high,
                               uint32_t durationMs)
{
    mwinLinuxPad* pad = nullptr;
    for (uint32_t i = 0; pads->pads != nullptr && i < pads->context->limits.gamepads; i++)
    {
        pad = pads->pads[i].fd >= 0 && pads->pads[i].slot == slot ? &pads->pads[i] : pad;
    }
    if (pad == nullptr)
    {
        return mwin_errorPlatform;
    }
    struct ff_effect effect = {.type = FF_RUMBLE, .id = pad->effect};
    effect.u.rumble.strong_magnitude = (uint16_t)(low * 65535.0f);
    effect.u.rumble.weak_magnitude = (uint16_t)(high * 65535.0f);
    effect.replay.length = (uint16_t)(durationMs < 65535u ? durationMs : 65535u);
    if (durationMs > 0 && ioctl(pad->fd, EVIOCSFF, &effect) < 0)
    {
        return mwin_errorPlatform;
    }
    pad->effect = durationMs > 0 ? effect.id : pad->effect;
    // Play the effect, or stop it.
    struct input_event play = {
        .type = EV_FF, .code = (uint16_t)pad->effect, .value = durationMs > 0 ? 1 : 0};
    if (pad->effect < 0)
    {
        return mwin_success;
    }
    return write(pad->fd, &play, sizeof(play)) == (ssize_t)sizeof(play) ? mwin_success
                                                                        : mwin_errorPlatform;
}
