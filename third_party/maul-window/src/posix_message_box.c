// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box on Linux, where no library of the desktop's is
// assumed: zenity (GNOME and most others), else kdialog (KDE), run with
// the text as arguments, never through a shell. Exit status 0 is OK or
// Yes; closing the box counts as Cancel or No.

#include "message_box.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

extern char** environ;

// Runs a program found on PATH and waits: its exit status, -1 when it
// is not there, -2 when it failed otherwise.
static int Run(char* const arguments[])
{
    pid_t child = 0;
    int spawned = posix_spawnp(&child, arguments[0], nullptr, nullptr, arguments, environ);
    if (spawned != 0)
    {
        return spawned == ENOENT ? -1 : -2;
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            return -2;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
}

// Text as a NUL-terminated argument after a prefix.
static char* Argument(char* out, size_t capacity, const char* prefix, const char* text,
                      size_t length)
{
    (void)snprintf(out, capacity, "%s%.*s", prefix, (int)length, text);
    return out;
}

static int Zenity(const mwinMessageBoxDef* def)
{
    static char* const kinds[] = {"--info", "--warning", "--error"};
    static char* const accepts[] = {"--ok-label=OK", "--ok-label=OK", "--ok-label=Yes"};
    static char* const refuses[] = {"--cancel-label=Cancel", "--cancel-label=Cancel",
                                    "--cancel-label=No"};
    char title[MWIN_MESSAGE_TITLE_BYTES + 16];
    char text[MWIN_MESSAGE_BYTES + 16];
    bool question = def->buttons != mwin_buttonsOk;
    char* arguments[] = {
        "zenity",
        question ? "--question" : kinds[def->kind],
        Argument(title, sizeof(title), "--title=", def->title, def->titleLength),
        Argument(text, sizeof(text), "--text=", def->message, def->messageLength),
        "--no-markup",
        question ? accepts[def->buttons] : nullptr,
        question ? refuses[def->buttons] : nullptr,
        nullptr,
    };
    return Run(arguments);
}

static int Kdialog(const mwinMessageBoxDef* def)
{
    static char* const kinds[] = {"--msgbox", "--sorry", "--error"};
    static char* const accepts[] = {"OK", "OK", "Yes"};
    static char* const refuses[] = {"Cancel", "Cancel", "No"};
    char title[MWIN_MESSAGE_TITLE_BYTES + 1];
    char text[MWIN_MESSAGE_BYTES + 1];
    bool question = def->buttons != mwin_buttonsOk;
    char* arguments[] = {
        "kdialog",
        "--title",
        Argument(title, sizeof(title), "", def->title, def->titleLength),
        question ? "--yesno" : kinds[def->kind],
        Argument(text, sizeof(text), "", def->message, def->messageLength),
        question ? "--yes-label" : nullptr,
        accepts[def->buttons],
        "--no-label",
        refuses[def->buttons],
        nullptr,
    };
    return Run(arguments);
}

mwinResult mwinPlatformMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut)
{
    int status = Zenity(def);
    // zenity answers 0 or 1; anything else was an error of its own.
    if (status == 0 || status == 1)
    {
        *acceptedOut = status == 0;
        return mwin_success;
    }
    if (status == -1)
    {
        status = Kdialog(def);
        if (status >= 0 && status <= 2)
        {
            *acceptedOut = status == 0;
            return mwin_success;
        }
    }
    return status == -1 ? mwin_errorUnsupported : mwin_errorPlatform;
}
