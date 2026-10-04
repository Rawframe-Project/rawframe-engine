// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The device directory watch, on inotify. Attribute changes count: udev
// makes a node readable after creating it.

#include "alsa_watch.h"

#include <stdalign.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

// Bytes read at once: room for many events with names.
#define EVENT_BYTES 4096

int maudAlsaOpenWatch(const char* directory)
{
    int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch >= 0 && inotify_add_watch(watch, directory, IN_CREATE | IN_DELETE | IN_ATTRIB) < 0)
    {
        close(watch);
        watch = -1;
    }
    return watch;
}

static bool IsCardNode(const char* name)
{
    return strncmp(name, "controlC", 8) == 0 || strncmp(name, "pcmC", 4) == 0;
}

bool maudAlsaTakeChanges(int watch)
{
    bool changed = false;
    alignas(struct inotify_event) char buffer[EVENT_BYTES];
    for (ssize_t length = watch >= 0 ? read(watch, buffer, sizeof(buffer)) : -1; length > 0;
         length = read(watch, buffer, sizeof(buffer)))
    {
        for (ssize_t at = 0; at + (ssize_t)sizeof(struct inotify_event) <= length;)
        {
            const struct inotify_event* event = (const struct inotify_event*)(buffer + at);
            changed = changed || (event->len > 0 && IsCardNode(event->name));
            at += (ssize_t)sizeof(struct inotify_event) + (ssize_t)event->len;
        }
    }
    return changed;
}

void maudAlsaCloseWatch(int watch)
{
    if (watch >= 0)
    {
        close(watch);
    }
}
