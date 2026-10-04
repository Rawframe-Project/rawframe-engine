// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Thread identities from the platform: the thread id on Windows, the
// pthread handle elsewhere. Neither is ever 0 for a running thread.

#include "thread.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

uintptr_t maudCurrentThread(void)
{
#if defined(_WIN32)
    return (uintptr_t)GetCurrentThreadId();
#else
    return (uintptr_t)pthread_self();
#endif
}
