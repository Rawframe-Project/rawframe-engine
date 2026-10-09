// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's apartment thread: two auto-reset events hand a
// call over and back.

#include "wasapi_apartment.h"

#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#include <windows.h>

static void Run(void* user)
{
    maudWasapiApartment* apartment = user;
    apartment->threadId = GetCurrentThreadId();
    apartment->joined = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    SetEvent(apartment->done);
    while (apartment->joined && WaitForSingleObject(apartment->posted, INFINITE) == WAIT_OBJECT_0 &&
           !apartment->quit)
    {
        apartment->call(apartment->user);
        SetEvent(apartment->done);
    }
    if (apartment->joined)
    {
        CoUninitialize();
    }
}

bool maudStartWasapiApartment(maudWasapiApartment* apartment)
{
    *apartment = (maudWasapiApartment){0};
    apartment->posted = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    apartment->done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (apartment->posted == nullptr || apartment->done == nullptr ||
        !maudStartWorker(&apartment->worker, Run, apartment, "maud-wasapi-com"))
    {
        maudStopWasapiApartment(apartment);
        return false;
    }
    WaitForSingleObject(apartment->done, INFINITE);
    if (!apartment->joined)
    {
        maudStopWasapiApartment(apartment);
        return false;
    }
    return true;
}

void maudCallInWasapiApartment(maudWasapiApartment* apartment, void (*call)(void* user), void* user)
{
    if (GetCurrentThreadId() == apartment->threadId)
    {
        call(user);
        return;
    }
    apartment->call = call;
    apartment->user = user;
    SetEvent(apartment->posted);
    WaitForSingleObject(apartment->done, INFINITE);
}

void maudStopWasapiApartment(maudWasapiApartment* apartment)
{
    if (apartment->worker.running)
    {
        apartment->quit = true;
        SetEvent(apartment->posted);
        maudJoinWorker(&apartment->worker);
    }
    if (apartment->posted != nullptr)
    {
        CloseHandle(apartment->posted);
    }
    if (apartment->done != nullptr)
    {
        CloseHandle(apartment->done);
    }
    *apartment = (maudWasapiApartment){0};
}
