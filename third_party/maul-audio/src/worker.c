// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Stream threads on POSIX systems and Windows.

#include "worker.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <string.h>
#include <windows.h>

static DWORD WINAPI RunWorker(void* user)
{
    maudWorker* worker = user;
    worker->run(worker->user);
    return 0;
}

// Names the thread through SetThreadDescription, which kernel32 has
// from Windows 10 version 1607; earlier systems keep it unnamed.
static void Name(HANDLE thread, const char* name)
{
    typedef HRESULT(WINAPI * Describe)(HANDLE thread, PCWSTR description);
    FARPROC found = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription");
    if (found == nullptr)
    {
        return;
    }
    Describe describe;
    memcpy((void*)&describe, (const void*)&found, sizeof(describe));
    wchar_t wide[16] = {0};
    for (size_t i = 0; i < 15 && name[i] != '\0'; ++i)
    {
        wide[i] = (wchar_t)name[i];
    }
    (void)describe(thread, wide);
}

bool maudStartWorker(maudWorker* worker, void (*run)(void* user), void* user, const char* name)
{
    worker->run = run;
    worker->user = user;
    worker->thread = CreateThread(nullptr, 0, RunWorker, worker, 0, nullptr);
    worker->running = worker->thread != nullptr;
    if (worker->running)
    {
        Name(worker->thread, name);
    }
    return worker->running;
}

void maudJoinWorker(maudWorker* worker)
{
    if (worker->running)
    {
        WaitForSingleObject(worker->thread, INFINITE);
        CloseHandle(worker->thread);
        worker->running = false;
    }
}

#else

#include <sched.h>

// Asks for the lowest real-time priority, which ordinary users are
// usually refused; the thread runs at normal priority then.
static void* RunWorker(void* user)
{
    maudWorker* worker = user;
    struct sched_param param = {.sched_priority = sched_get_priority_min(SCHED_FIFO)};
    (void)pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    worker->run(worker->user);
    return nullptr;
}

bool maudStartWorker(maudWorker* worker, void (*run)(void* user), void* user, const char* name)
{
    worker->run = run;
    worker->user = user;
    worker->running = pthread_create(&worker->thread, nullptr, RunWorker, worker) == 0;
    if (worker->running)
    {
        (void)pthread_setname_np(worker->thread, name);
    }
    return worker->running;
}

void maudJoinWorker(maudWorker* worker)
{
    if (worker->running)
    {
        pthread_join(worker->thread, nullptr);
        worker->running = false;
    }
}

#endif
