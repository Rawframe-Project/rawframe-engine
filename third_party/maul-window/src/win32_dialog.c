// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 file dialogs.

// The interfaces' C call macros, before any header of Windows'.
#define COBJMACROS

#include "win32_dialog.h"

#include "allocator.h"
#include "dialog.h"

#include "maul-unicode/encoding.h"

#include <shobjidl.h>
#include <string.h>

// The interfaces and classes, defined here: no import library needed.
static const CLSID s_openClass = {
    0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};
static const CLSID s_saveClass = {
    0xC0B4E2F3, 0xBA21, 0x4773, {0x8D, 0xBA, 0x33, 0x5E, 0xC9, 0x46, 0xEB, 0x8B}};
static const IID s_fileDialog = {
    0x42F85136, 0xDB7E, 0x439C, {0x85, 0xF1, 0xE4, 0x07, 0x5D, 0x13, 0x5F, 0xC8}};
static const IID s_openDialog = {
    0xD57C7288, 0xD4AD, 0x4768, {0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60}};
static const IID s_shellItem = {
    0x43826D1E, 0xE718, 0x42EE, {0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE}};
static const IID s_oleWindow = {0x00000114, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

int mwinWin32AskDialog(mwinWin32Window* window)
{
    window->dialogWaiting = true;
    return -1;
}

// UTF-8 as UTF-16 with its terminator at *at, which moves on; a path
// takes Windows' separators.
static WCHAR* Widen(WCHAR** at, const char* text, size_t length, bool path)
{
    WCHAR* start = *at;
    size_t count = 0;
    (void)muniConvertUtf8ToUtf16(text, length, (uint16_t*)start, length, muni_convertReplace,
                                 &count);
    for (size_t i = 0; path && i < count; i++)
    {
        start[i] = start[i] == L'/' ? L'\\' : start[i];
    }
    start[count] = L'\0';
    *at += count + 1;
    return start;
}

// "png;jpg" as "*.png;*.jpg", into a buffer of at most three bytes an
// extension byte and one more.
static size_t Spec(const char* extensions, char* spec)
{
    size_t at = 0;
    bool starting = true;
    for (const char* c = extensions; *c != '\0'; c++)
    {
        if (starting)
        {
            spec[at++] = '*';
            spec[at++] = '.';
        }
        spec[at++] = *c;
        starting = *c == ';';
    }
    spec[at] = '\0';
    return at;
}

// The UTF-16 units the def's text takes, terminators included.
static size_t Units(const mwinDialogCopy* copy)
{
    size_t units = copy->titleLength + copy->folderLength + copy->nameLength + 3;
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        units += strlen(copy->filters[i].name) + 4 * strlen(copy->filters[i].extensions) + 3;
    }
    return units;
}

static HRESULT SetFilters(IFileDialog* dialog, const mwinDialogCopy* copy, WCHAR** at)
{
    COMDLG_FILTERSPEC specs[MWIN_DIALOG_FILTERS];
    char spec[3 * MWIN_DIALOG_FILTER_BYTES + 1];
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        const mwinDialogFilter* filter = &copy->filters[i];
        specs[i].pszName = Widen(at, filter->name, strlen(filter->name), false);
        specs[i].pszSpec = Widen(at, spec, Spec(filter->extensions, spec), false);
    }
    HRESULT result = IFileDialog_SetFileTypes(dialog, copy->filterCount, specs);
    if (SUCCEEDED(result))
    {
        result = IFileDialog_SetFileTypeIndex(dialog, 1);
    }
    // A name typed without an extension takes the first filter's.
    if (SUCCEEDED(result) && copy->kind == mwin_dialogSave)
    {
        const char* first = copy->filters[0].extensions;
        result =
            IFileDialog_SetDefaultExtension(dialog, Widen(at, first, strcspn(first, ";"), false));
    }
    return result;
}

static FILEOPENDIALOGOPTIONS Options(mwinDialogKind kind)
{
    FILEOPENDIALOGOPTIONS options = FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
    switch (kind)
    {
    case mwin_dialogOpenMany:
        return options | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT;
    case mwin_dialogSave:
        return options | FOS_OVERWRITEPROMPT;
    case mwin_dialogFolder:
        return options | FOS_PICKFOLDERS;
    default:
        return options | FOS_FILEMUSTEXIST;
    }
}

static HRESULT Configure(IFileDialog* dialog, const mwinDialogCopy* copy, WCHAR* units)
{
    WCHAR* at = units;
    FILEOPENDIALOGOPTIONS options = 0;
    HRESULT result = IFileDialog_GetOptions(dialog, &options);
    if (SUCCEEDED(result))
    {
        result = IFileDialog_SetOptions(dialog, options | Options(copy->kind));
    }
    if (SUCCEEDED(result) && copy->titleLength > 0)
    {
        result = IFileDialog_SetTitle(dialog, Widen(&at, copy->title, copy->titleLength, false));
    }
    if (SUCCEEDED(result) && copy->kind == mwin_dialogSave && copy->nameLength > 0)
    {
        result = IFileDialog_SetFileName(dialog, Widen(&at, copy->name, copy->nameLength, false));
    }
    if (SUCCEEDED(result) && copy->filterCount > 0)
    {
        result = SetFilters(dialog, copy, &at);
    }
    IShellItem* folder = nullptr;
    // A folder that does not exist leaves the platform's choice.
    if (SUCCEEDED(result) && copy->folderLength > 0 &&
        SUCCEEDED(SHCreateItemFromParsingName(Widen(&at, copy->folder, copy->folderLength, true),
                                              nullptr, &s_shellItem, (void**)&folder)))
    {
        result = IFileDialog_SetFolder(dialog, folder);
        IShellItem_Release(folder);
    }
    return result;
}

// Adds an item's path to the dialog's answer.
static void AddItem(mwinContext* context, IShellItem* item)
{
    WCHAR* path = nullptr;
    if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path)))
    {
        mwinAddDialogFileUtf16(context, (const uint16_t*)path, wcslen(path));
        CoTaskMemFree(path);
    }
    else
    {
        // An item without a path names no file the program can open.
        mwinAddDialogFile(context, "", 0);
    }
}

static void Collect(mwinContext* context, IFileDialog* dialog, mwinDialogKind kind)
{
    IFileOpenDialog* open = nullptr;
    IShellItemArray* items = nullptr;
    IShellItem* item = nullptr;
    mwinBeginDialog(context);
    if (kind == mwin_dialogOpenMany &&
        SUCCEEDED(IFileDialog_QueryInterface(dialog, &s_openDialog, (void**)&open)))
    {
        DWORD count = 0;
        if (SUCCEEDED(IFileOpenDialog_GetResults(open, &items)) &&
            SUCCEEDED(IShellItemArray_GetCount(items, &count)))
        {
            for (DWORD i = 0; i < count; i++)
            {
                if (SUCCEEDED(IShellItemArray_GetItemAt(items, i, &item)))
                {
                    AddItem(context, item);
                    IShellItem_Release(item);
                }
            }
        }
        if (items != nullptr)
        {
            IShellItemArray_Release(items);
        }
        IFileOpenDialog_Release(open);
    }
    else if (SUCCEEDED(IFileDialog_GetResult(dialog, &item)))
    {
        AddItem(context, item);
        IShellItem_Release(item);
    }
}

// The dialog's request, while it still waits.
static const mwinRequest* Waiting(const mwinWin32Platform* platform)
{
    const mwinContext* context = platform->context;
    const mwinWindow* window = &context->windows[platform->dialogSlot];
    const mwinRequest* request = &window->requests[platform->dialogRequest];
    bool waiting = window->status == mwin_slotLive && request->status == mwin_requestActive &&
                   request->generation == platform->dialogGeneration;
    return waiting ? request : nullptr;
}

void mwinWin32DialogTick(mwinWin32Platform* platform)
{
    mwinContext* context = platform->context;
    mwinRunCriticalFrame(context);
    IOleWindow* window = nullptr;
    HWND hwnd = nullptr;
    bool closing =
        platform->dialog != nullptr && (context->stopping || Waiting(platform) == nullptr);
    if (closing &&
        SUCCEEDED(IFileDialog_QueryInterface(platform->dialog, &s_oleWindow, (void**)&window)))
    {
        if (SUCCEEDED(IOleWindow_GetWindow(window, &hwnd)) && hwnd != nullptr)
        {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        IOleWindow_Release(window);
    }
}

// Shows the dialog, frames going on from the owner's timer: how it
// ended, the paths gathered when done.
static mwinOutcome Show(mwinWin32Platform* platform, HWND owner, const mwinDialogCopy* copy)
{
    mwinContext* context = platform->context;
    size_t size = Units(copy) * sizeof(WCHAR);
    WCHAR* units = mwinAllocate(&context->allocator, size, alignof(WCHAR));
    IFileDialog* dialog = nullptr;
    const CLSID* kind = copy->kind == mwin_dialogSave ? &s_saveClass : &s_openClass;
    HRESULT result = units == nullptr ? E_OUTOFMEMORY
                                      : CoCreateInstance(kind, nullptr, CLSCTX_INPROC_SERVER,
                                                         &s_fileDialog, (void**)&dialog);
    if (SUCCEEDED(result))
    {
        result = Configure(dialog, copy, units);
    }
    if (SUCCEEDED(result))
    {
        platform->dialog = dialog;
        SetTimer(owner, MWIN_WIN32_DIALOG_TIMER, USER_TIMER_MINIMUM, nullptr);
        result = IFileDialog_Show(dialog, owner);
        KillTimer(owner, MWIN_WIN32_DIALOG_TIMER);
        platform->dialog = nullptr;
    }
    if (SUCCEEDED(result) && Waiting(platform) != nullptr)
    {
        Collect(context, dialog, copy->kind);
    }
    if (dialog != nullptr)
    {
        IFileDialog_Release(dialog);
    }
    if (units != nullptr)
    {
        mwinRelease(&context->allocator, units, size, alignof(WCHAR));
    }
    return result == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? mwin_outcomeCancelled
           : SUCCEEDED(result)                           ? mwin_outcomeDone
                                                         : mwin_outcomeFailed;
}

void mwinWin32ShowDialogs(mwinWin32Platform* platform)
{
    mwinContext* context = platform->context;
    for (uint32_t slot = 0; slot < context->limits.windows; slot++)
    {
        mwinWin32Window* window = &platform->windows[slot];
        if (!window->dialogWaiting)
        {
            continue;
        }
        window->dialogWaiting = false;
        const mwinWindow* core = &context->windows[slot];
        int32_t request = core->status == mwin_slotLive
                              ? mwinFindActiveRequest(core, context->limits.requestsPerWindow,
                                                      mwin_requestFileDialog)
                              : -1;
        if (request < 0)
        {
            continue;
        }
        platform->dialogSlot = slot;
        platform->dialogRequest = (uint32_t)request;
        platform->dialogGeneration = core->requests[request].generation;
        mwinOutcome outcome = Show(platform, window->hwnd, core->requests[request].value.dialog);
        // The program may have ended the request while the dialog showed.
        if (Waiting(platform) != nullptr)
        {
            outcome = mwinSettleDialog(context, slot, (uint32_t)request, outcome);
            mwinComplete(context, slot, (uint32_t)request, outcome);
        }
        else
        {
            mwinBeginDialog(context);
        }
        // One a pump: the program sees each answer before the next shows.
        return;
    }
}
