// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's motion sensors.

#include "linux_motion.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define LONG_BITS   (sizeof(unsigned long) * CHAR_BIT)
#define LONGS(bits) (((bits) + LONG_BITS - 1) / LONG_BITS)

static bool Has(const unsigned long* bits, unsigned bit)
{
    return ((bits[bit / LONG_BITS] >> (bit % LONG_BITS)) & 1u) != 0;
}

void mwinLinuxIdentityOf(int fd, mwinLinuxPadIdentity* identity)
{
    *identity = (mwinLinuxPadIdentity){0};
    (void)ioctl(fd, EVIOCGUNIQ(sizeof(identity->uniq) - 1), identity->uniq);
    (void)ioctl(fd, EVIOCGPHYS(sizeof(identity->phys) - 1), identity->phys);
    struct input_id id = {0};
    (void)ioctl(fd, EVIOCGID, &id);
    identity->vendor = id.vendor;
}

// The axes' current values, and their factors from their resolutions:
// false where one is missing or says nothing.
static bool ReadAxes(int fd, mwinLinuxMotionEvents* events)
{
    for (unsigned i = 0; i < MWIN_LINUX_MOTION_AXES; i++)
    {
        struct input_absinfo info;
        unsigned code = i < 3 ? ABS_X + i : ABS_RX + i - 3;
        if (ioctl(fd, EVIOCGABS(code), &info) < 0 ||
            !mwinLinuxMotionAxis(events, i, info.value, info.resolution))
        {
            return false;
        }
    }
    return true;
}

bool mwinLinuxOpenMotion(mwinLinuxMotion* motion, int node, const mwinLinuxPadIdentity* pad)
{
    char path[32];
    (void)snprintf(path, sizeof(path), "/dev/input/event%d", node);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
    {
        return false;
    }
    unsigned long props[LONGS(INPUT_PROP_CNT)] = {0};
    (void)ioctl(fd, EVIOCGPROP(sizeof(props)), props);
    mwinLinuxPadIdentity identity;
    mwinLinuxIdentityOf(fd, &identity);
    mwinLinuxMotion found = {.fd = fd, .node = node};
    mwinLinuxMotionStart(&found.events, pad);
    if (!Has(props, INPUT_PROP_ACCELEROMETER) || !mwinLinuxSameController(pad, &identity) ||
        !ReadAxes(fd, &found.events))
    {
        close(fd);
        return false;
    }
    int clock = CLOCK_MONOTONIC;
    (void)ioctl(fd, EVIOCSCLOCKID, &clock);
    *motion = found;
    return true;
}

bool mwinLinuxFindMotion(mwinLinuxMotion* motion, const mwinLinuxPadIdentity* pad)
{
    bool found = false;
    DIR* directory = opendir("/dev/input");
    for (struct dirent* entry = directory != nullptr ? readdir(directory) : nullptr;
         entry != nullptr && !found; entry = readdir(directory))
    {
        int node = -1;
        char end = 0;
        if (sscanf(entry->d_name, "event%d%c", &node, &end) == 1 && node >= 0)
        {
            found = mwinLinuxOpenMotion(motion, node, pad);
        }
    }
    if (directory != nullptr)
    {
        closedir(directory);
    }
    return found;
}

bool mwinLinuxReadMotion(mwinLinuxMotion* motion, mwinContext* context, uint32_t slot)
{
    struct input_event events[32];
    for (;;)
    {
        ssize_t bytes = read(motion->fd, events, sizeof(events));
        if (bytes < 0)
        {
            return errno == EAGAIN || errno == EINTR;
        }
        for (size_t i = 0; i < (size_t)bytes / sizeof(events[0]); i++)
        {
            const struct input_event* event = &events[i];
            uint64_t timeNs = (uint64_t)event->input_event_sec * 1000000000u +
                              (uint64_t)event->input_event_usec * 1000u;
            mwinLinuxMotionSample sample;
            switch (mwinLinuxMotionEvent(&motion->events, event->type, event->code, event->value,
                                         timeNs, &sample))
            {
            case mwin_motionSample:
                mwinPostGamepadMotion(context, slot, sample.acceleration, sample.rate,
                                      sample.timeNs);
                break;
            case mwin_motionResync:
                (void)ReadAxes(motion->fd, &motion->events);
                break;
            case mwin_motionNothing:
                break;
            }
        }
    }
}

void mwinLinuxCloseMotion(mwinLinuxMotion* motion)
{
    if (motion->fd >= 0)
    {
        close(motion->fd);
    }
    *motion = (mwinLinuxMotion){.fd = -1, .node = -1};
}
