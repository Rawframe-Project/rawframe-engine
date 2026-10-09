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
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define LONG_BITS   (sizeof(unsigned long) * CHAR_BIT)
#define LONGS(bits) (((bits) + LONG_BITS - 1) / LONG_BITS)

#define STANDARD_GRAVITY   9.80665f
#define RADIANS_PER_DEGREE 0.017453292f
#define VENDOR_NINTENDO    0x057E

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

// Whether two devices are parts of one controller: the same unique id
// where both have one (Bluetooth pads share the adapter's path), else
// the same path.
static bool SameController(const mwinLinuxPadIdentity* a, const mwinLinuxPadIdentity* b)
{
    if (a->uniq[0] != '\0' && b->uniq[0] != '\0')
    {
        return strcmp(a->uniq, b->uniq) == 0;
    }
    return a->uniq[0] == '\0' && b->uniq[0] == '\0' && a->phys[0] != '\0' &&
           strcmp(a->phys, b->phys) == 0;
}

// The axes' factors from their resolutions: false where one is missing
// or says nothing.
static bool ReadScales(int fd, mwinLinuxMotion* motion)
{
    for (unsigned i = 0; i < 6; i++)
    {
        struct input_absinfo info;
        unsigned code = i < 3 ? ABS_X + i : ABS_RX + i - 3;
        if (ioctl(fd, EVIOCGABS(code), &info) < 0 || info.resolution <= 0)
        {
            return false;
        }
        float unit = i < 3 ? STANDARD_GRAVITY : RADIANS_PER_DEGREE;
        motion->scale[i] = unit / (float)info.resolution;
        motion->raw[i] = info.value;
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
    mwinLinuxMotion found = {.fd = fd, .node = node, .nintendo = pad->vendor == VENDOR_NINTENDO};
    if (!Has(props, INPUT_PROP_ACCELEROMETER) || !SameController(pad, &identity) ||
        !ReadScales(fd, &found))
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

// Posts the axes as one sample, in the contract's frame.
static void Post(mwinLinuxMotion* motion, mwinContext* context, uint32_t slot, uint64_t timeNs)
{
    float values[6];
    for (int i = 0; i < 6; i++)
    {
        values[i] = (float)motion->raw[i] * motion->scale[i];
    }
    float acceleration[3] = {values[0], values[1], values[2]};
    float rate[3] = {values[3], values[4], values[5]};
    if (motion->nintendo)
    {
        acceleration[0] = -values[1];
        acceleration[1] = values[2];
        acceleration[2] = -values[0];
        rate[0] = -values[4];
        rate[1] = values[5];
        rate[2] = -values[3];
    }
    mwinPostGamepadMotion(context, slot, acceleration, rate,
                          motion->stamped ? motion->timeNs : timeNs);
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
            if (event->type == EV_ABS && event->code <= ABS_Z)
            {
                motion->raw[event->code - ABS_X] = event->value;
            }
            else if (event->type == EV_ABS && event->code >= ABS_RX && event->code <= ABS_RZ)
            {
                motion->raw[3 + event->code - ABS_RX] = event->value;
            }
            else if (event->type == EV_MSC && event->code == MSC_TIMESTAMP)
            {
                // Microseconds that wrap: the difference counts, the first
                // stamp a start that is not 0, which would mean none.
                uint32_t stamp = (uint32_t)event->value;
                motion->timeNs +=
                    motion->stamped ? (uint64_t)(stamp - motion->stamp) * 1000u : 1000u;
                motion->stamp = stamp;
                motion->stamped = true;
            }
            else if (event->type == EV_SYN && event->code == SYN_REPORT)
            {
                Post(motion, context, slot,
                     (uint64_t)event->input_event_sec * 1000000000u +
                         (uint64_t)event->input_event_usec * 1000u);
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
