// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 drag and drop.

#include "win32_drop.h"

#include "allocator.h"

#include <ole2.h>
#include <shellapi.h>
#include <stddef.h>
#include <wchar.h>

static_assert(offsetof(mwinWin32DropTarget, target) == 0, "the interface comes first");

// IUnknown's and IDropTarget's ids, here rather than from uuid.lib.
static const IID s_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID s_dropTarget = {0x00000122, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

// The target an interface of the backend's is the first member of.
static mwinWin32DropTarget* TargetOf(IDropTarget* target)
{
    return (mwinWin32DropTarget*)target;
}

static HRESULT STDMETHODCALLTYPE QueryInterface(IDropTarget* target, REFIID id, void** out)
{
    bool known =
        memcmp(id, &s_unknown, sizeof(IID)) == 0 || memcmp(id, &s_dropTarget, sizeof(IID)) == 0;
    *out = known ? target : nullptr;
    return known ? S_OK : E_NOINTERFACE;
}

// The target lives as long as its window; references change nothing.
static ULONG STDMETHODCALLTYPE AddRef(IDropTarget* target)
{
    (void)target;
    return 1;
}

static bool Has(IDataObject* data, CLIPFORMAT format)
{
    FORMATETC wanted = {format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return data->lpVtbl->QueryGetData(data, &wanted) == S_OK;
}

// A point on the screen in the window's client area, in pixels.
static POINT ClientPoint(const mwinWin32Window* window, POINTL at)
{
    POINT point = {at.x, at.y};
    ScreenToClient(window->hwnd, &point);
    return point;
}

static mwinPosition PositionOf(const mwinWin32Window* window, POINT point)
{
    float scale = mwinWin32Scale(window->dpi);
    return (mwinPosition){(float)point.x / scale, (float)point.y / scale};
}

static void PostDrag(mwinWin32Window* window, mwinEventType type, POINT point)
{
    mwinWin32DropTarget* drop = &window->drop;
    drop->x = point.x;
    drop->y = point.y;
    mwinEvent event = {.type = type, .timeNs = mwinWin32Now()};
    event.data.drag = (mwinDragEvent){PositionOf(window, point), drop->contents};
    mwinPost(window->platform->context, window->slot, &event);
}

static HRESULT STDMETHODCALLTYPE DragEnter(IDropTarget* target, IDataObject* data, DWORD keys,
                                           POINTL at, DWORD* effect)
{
    (void)keys;
    mwinWin32DropTarget* drop = TargetOf(target);
    drop->contents = (Has(data, CF_HDROP) ? mwin_dragFiles : 0) |
                     (Has(data, CF_UNICODETEXT) ? mwin_dragText : 0);
    *effect = drop->contents != 0 ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (drop->contents != 0)
    {
        PostDrag(drop->window, mwin_eventDragEntered, ClientPoint(drop->window, at));
    }
    return S_OK;
}

// OLE calls it again and again while the drag rests.
static HRESULT STDMETHODCALLTYPE DragOver(IDropTarget* target, DWORD keys, POINTL at, DWORD* effect)
{
    (void)keys;
    mwinWin32DropTarget* drop = TargetOf(target);
    *effect = drop->contents != 0 ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    POINT point = ClientPoint(drop->window, at);
    if (drop->contents != 0 && (point.x != drop->x || point.y != drop->y))
    {
        PostDrag(drop->window, mwin_eventDragMoved, point);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DragLeave(IDropTarget* target)
{
    mwinWin32DropTarget* drop = TargetOf(target);
    if (drop->contents != 0)
    {
        PostDrag(drop->window, mwin_eventDragLeft, (POINT){drop->x, drop->y});
    }
    drop->contents = 0;
    return S_OK;
}

// Adds the paths of a file drop to the context's drop.
static void GatherFiles(mwinContext* context, HDROP files)
{
    UINT count = DragQueryFileW(files, 0xFFFFFFFFu, nullptr, 0);
    for (UINT i = 0; i < count; i++)
    {
        UINT length = DragQueryFileW(files, i, nullptr, 0);
        size_t bytes = ((size_t)length + 1) * sizeof(WCHAR);
        WCHAR* path = mwinAllocate(&context->allocator, bytes, alignof(WCHAR));
        if (path == nullptr)
        {
            context->dropping.truncated = true;
            continue;
        }
        length = DragQueryFileW(files, i, path, length + 1);
        mwinAddDroppedFileUtf16(context, (const uint16_t*)path, length);
        mwinRelease(&context->allocator, path, bytes, alignof(WCHAR));
    }
}

// Adds what a data object holds in a format to the context's drop.
static void Gather(mwinContext* context, IDataObject* data, CLIPFORMAT format)
{
    FORMATETC wanted = {format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {0};
    if (data->lpVtbl->GetData(data, &wanted, &medium) != S_OK)
    {
        return;
    }
    void* bytes = GlobalLock(medium.hGlobal);
    if (bytes != nullptr && format == CF_HDROP)
    {
        GatherFiles(context, (HDROP)medium.hGlobal);
    }
    else if (bytes != nullptr)
    {
        // Another program's memory need not end with a terminator.
        size_t length = wcsnlen(bytes, GlobalSize(medium.hGlobal) / sizeof(WCHAR));
        mwinSetDroppedTextUtf16(context, bytes, length);
    }
    if (bytes != nullptr)
    {
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
}

static HRESULT STDMETHODCALLTYPE Drop(IDropTarget* target, IDataObject* data, DWORD keys, POINTL at,
                                      DWORD* effect)
{
    (void)keys;
    mwinWin32DropTarget* drop = TargetOf(target);
    mwinWin32Window* window = drop->window;
    mwinContext* context = window->platform->context;
    *effect = drop->contents != 0 ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (drop->contents != 0)
    {
        mwinBeginDrop(context);
        Gather(context, data, CF_HDROP);
        Gather(context, data, CF_UNICODETEXT);
        mwinFinishDrop(context, window->slot, PositionOf(window, ClientPoint(window, at)),
                       mwinWin32Now());
    }
    drop->contents = 0;
    return S_OK;
}

static const IDropTargetVtbl s_functions = {
    QueryInterface, AddRef, AddRef, DragEnter, DragOver, DragLeave, Drop,
};

void mwinWin32StartOle(mwinWin32Platform* platform)
{
    // S_FALSE: OLE was started already, and is counted again.
    platform->ole = SUCCEEDED(OleInitialize(nullptr));
}

void mwinWin32StopOle(mwinWin32Platform* platform)
{
    if (platform->ole)
    {
        OleUninitialize();
    }
    platform->ole = false;
}

void mwinWin32AttachDrop(mwinWin32Window* window)
{
    mwinWin32DropTarget* drop = &window->drop;
    *drop = (mwinWin32DropTarget){.target = {&s_functions}, .window = window};
    drop->registered =
        window->platform->ole && RegisterDragDrop(window->hwnd, &drop->target) == S_OK;
    if (!drop->registered)
    {
        DragAcceptFiles(window->hwnd, TRUE);
    }
}

void mwinWin32DetachDrop(mwinWin32Window* window)
{
    if (window->drop.registered)
    {
        RevokeDragDrop(window->hwnd);
    }
    window->drop.registered = false;
}

bool mwinWin32HandleDropFiles(mwinWin32Window* window, UINT message, WPARAM wParam)
{
    if (message != WM_DROPFILES)
    {
        return false;
    }
    HDROP files = mwinWin32Pointer((LONG_PTR)wParam);
    mwinContext* context = window->platform->context;
    POINT point = {0, 0};
    DragQueryPoint(files, &point);
    mwinBeginDrop(context);
    GatherFiles(context, files);
    mwinFinishDrop(context, window->slot, PositionOf(window, point), mwinWin32Now());
    DragFinish(files);
    return true;
}
