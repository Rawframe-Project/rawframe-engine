// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32's touch keyboard and input purpose.

#include "win32_touch_keyboard.h"

#include <shobjidl.h>
#include <string.h>

// The input scopes of inputscope.h the purposes map to.
enum
{
    scopeDefault = 0,
    scopeUrl = 1,
    scopeEmail = 5,
    scopeNumber = 29,
    scopePassword = 31,
};

// The first six methods of every runtime interface: IUnknown's, then
// IInspectable's, which nothing here calls.
#define INSPECTABLE                                                                                \
    void* QueryInterface;                                                                          \
    void* AddRef;                                                                                  \
    ULONG(STDMETHODCALLTYPE* Release)(void* self);                                                 \
    void* GetIids;                                                                                 \
    void* GetRuntimeClassName;                                                                     \
    void* GetTrustLevel;

typedef struct InputPane
{
    const struct
    {
        INSPECTABLE
        HRESULT(STDMETHODCALLTYPE* tryShow)(struct InputPane* self, boolean* accepted);
        HRESULT(STDMETHODCALLTYPE* tryHide)(struct InputPane* self, boolean* accepted);
    }* v;
} InputPane;

typedef struct InputPaneInterop
{
    const struct
    {
        INSPECTABLE
        HRESULT(STDMETHODCALLTYPE* getForWindow)(struct InputPaneInterop* self, HWND window,
                                                 const IID* iid, void** pane);
    }* v;
} InputPaneInterop;

// IInputPaneInterop and IInputPane2, as inputpaneinterop.h and
// Windows.UI.ViewManagement.h define them.
static const IID s_iidInterop = {
    0x75CF2C57, 0x9195, 0x4931, {0x83, 0x32, 0xF0, 0xB4, 0x09, 0xE9, 0x16, 0xAF}};
static const IID s_iidPane2 = {
    0x8A6B3F26, 0x7090, 0x4793, {0x94, 0x4C, 0xC3, 0xF2, 0xCD, 0xE2, 0x62, 0x76}};

// FrameworkInputPane, IFrameworkInputPane and IFrameworkInputPaneHandler,
// as shobjidl_core.h defines them, and IUnknown.
static const CLSID s_clsidFramework = {
    0xD5120AA3, 0x46BA, 0x44C5, {0x82, 0x2D, 0xCA, 0x80, 0x92, 0xC1, 0xFC, 0x72}};
static const IID s_iidFramework = {
    0x5752238B, 0x24F0, 0x495A, {0x82, 0xF1, 0x2F, 0xD5, 0x93, 0x05, 0x67, 0x96}};
static const IID s_iidHandler = {
    0x226C537B, 0x1E76, 0x4D9E, {0xA7, 0x60, 0x33, 0xDB, 0x29, 0x92, 0x2F, 0x18}};
static const IID s_iidUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

// A library's function, its bytes copied: a FARPROC is no function of
// the right type to cast.
static bool Load(HMODULE library, const char* name, void* function, size_t size)
{
    FARPROC found = library != nullptr ? GetProcAddress(library, name) : nullptr;
    memcpy(function, (const void*)&found, size);
    return found != nullptr;
}

// Loads what the requests use, once; each part may be missing.
static void LoadOnce(mwinWin32Platform* platform)
{
    if (platform->keyboardTried)
    {
        return;
    }
    platform->keyboardTried = true;
    platform->msctf = LoadLibraryW(L"msctf.dll");
    (void)Load(platform->msctf, "SetInputScope", (void*)&platform->setInputScope,
               sizeof(platform->setInputScope));
    platform->combase = LoadLibraryW(L"combase.dll");
    if (!Load(platform->combase, "RoGetActivationFactory", (void*)&platform->activate,
              sizeof(platform->activate)) ||
        !Load(platform->combase, "WindowsCreateString", (void*)&platform->makeString,
              sizeof(platform->makeString)) ||
        !Load(platform->combase, "WindowsDeleteString", (void*)&platform->deleteString,
              sizeof(platform->deleteString)))
    {
        platform->activate = nullptr;
    }
}

static int ScopeOf(mwinInputPurpose purpose)
{
    switch (purpose)
    {
    case mwin_purposeNumber:
        return scopeNumber;
    case mwin_purposeEmail:
        return scopeEmail;
    case mwin_purposePassword:
        return scopePassword;
    case mwin_purposeUrl:
        return scopeUrl;
    default:
        return scopeDefault;
    }
}

// The window's InputPane, or NULL where Windows has none for desktop
// windows (before Windows 10 1607).
static InputPane* PaneOf(const mwinWin32Platform* platform, HWND hwnd)
{
    static const WCHAR name[] = L"Windows.UI.ViewManagement.InputPane";
    void* string = nullptr;
    InputPaneInterop* interop = nullptr;
    InputPane* pane = nullptr;
    if (platform->activate == nullptr ||
        FAILED(platform->makeString(name, (UINT32)(sizeof(name) / sizeof(name[0]) - 1), &string)))
    {
        return nullptr;
    }
    if (SUCCEEDED(platform->activate(string, &s_iidInterop, (void**)&interop)))
    {
        if (FAILED(interop->v->getForWindow(interop, hwnd, &s_iidPane2, (void**)&pane)))
        {
            pane = nullptr;
        }
        (void)interop->v->Release(interop);
    }
    (void)platform->deleteString(string);
    return pane;
}

// The handler a pointer to its interface is.
static mwinWin32PaneHandler* HandlerOf(IFrameworkInputPaneHandler* handler)
{
    return (mwinWin32PaneHandler*)handler;
}

static HRESULT STDMETHODCALLTYPE HandlerQuery(IFrameworkInputPaneHandler* handler, REFIID id,
                                              void** out)
{
    bool known =
        memcmp(id, &s_iidUnknown, sizeof(IID)) == 0 || memcmp(id, &s_iidHandler, sizeof(IID)) == 0;
    *out = known ? handler : nullptr;
    return known ? S_OK : E_NOINTERFACE;
}

// The handler lives as long as its window; references change nothing.
static ULONG STDMETHODCALLTYPE HandlerAddRef(IFrameworkInputPaneHandler* handler)
{
    (void)handler;
    return 1;
}

static HRESULT STDMETHODCALLTYPE HandlerShowing(IFrameworkInputPaneHandler* handler, RECT* keyboard,
                                                BOOL ensureVisible)
{
    (void)ensureVisible;
    mwinWin32CoverWindow(HandlerOf(handler)->window, keyboard);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE HandlerHiding(IFrameworkInputPaneHandler* handler,
                                               BOOL ensureVisible)
{
    (void)ensureVisible;
    mwinWin32CoverWindow(HandlerOf(handler)->window, nullptr);
    return S_OK;
}

static const IFrameworkInputPaneHandlerVtbl s_handler = {
    HandlerQuery, HandlerAddRef, HandlerAddRef, HandlerShowing, HandlerHiding,
};

// Follows the touch keyboard over the window, from its first request.
static void Advise(mwinWin32Window* window)
{
    mwinWin32PaneHandler* handler = &window->paneHandler;
    if (handler->methods != nullptr)
    {
        return;
    }
    *handler = (mwinWin32PaneHandler){.methods = &s_handler, .window = window};
    IFrameworkInputPane* pane = nullptr;
    if (FAILED(CoCreateInstance(&s_clsidFramework, nullptr, CLSCTX_INPROC_SERVER, &s_iidFramework,
                                (void**)&pane)))
    {
        return;
    }
    if (FAILED(pane->lpVtbl->AdviseWithHWND(
            pane, window->hwnd, (IFrameworkInputPaneHandler*)handler, &handler->cookie)))
    {
        (void)pane->lpVtbl->Release(pane);
        return;
    }
    handler->pane = pane;
}

void mwinWin32DetachPane(mwinWin32Window* window)
{
    IFrameworkInputPane* pane = window->paneHandler.pane;
    if (pane != nullptr)
    {
        (void)pane->lpVtbl->Unadvise(pane, window->paneHandler.cookie);
        (void)pane->lpVtbl->Release(pane);
    }
    window->paneHandler = (mwinWin32PaneHandler){0};
}

void mwinWin32CoverWindow(mwinWin32Window* window, const RECT* keyboard)
{
    mwinRect covered = {0};
    if (keyboard != nullptr)
    {
        // The keyboard's part of the client area, in logical units.
        POINT origin = {0, 0};
        (void)ClientToScreen(window->hwnd, &origin);
        float scale = (float)window->dpi / (float)USER_DEFAULT_SCREEN_DPI;
        LONG left = keyboard->left - origin.x > 0 ? keyboard->left - origin.x : 0;
        LONG top = keyboard->top - origin.y > 0 ? keyboard->top - origin.y : 0;
        LONG right = keyboard->right - origin.x < (LONG)window->width ? keyboard->right - origin.x
                                                                      : (LONG)window->width;
        LONG bottom = keyboard->bottom - origin.y < (LONG)window->height
                          ? keyboard->bottom - origin.y
                          : (LONG)window->height;
        if (right > left && bottom > top && scale > 0.0f)
        {
            covered = (mwinRect){(float)left / scale, (float)top / scale,
                                 (float)(right - left) / scale, (float)(bottom - top) / scale};
        }
    }
    const mwinRect* known = &window->covered;
    if (covered.x == known->x && covered.y == known->y && covered.width == known->width &&
        covered.height == known->height)
    {
        return;
    }
    window->covered = covered;
    mwinEvent event = {.type = mwin_eventVirtualKeyboardChanged, .timeNs = mwinWin32Now()};
    event.data.rect = covered;
    mwinPost(window->platform->context, window->slot, &event);
}

mwinOutcome mwinWin32SetTouchKeyboard(mwinWin32Window* window, bool visible,
                                      mwinInputPurpose purpose)
{
    mwinWin32Platform* platform = window->platform;
    LoadOnce(platform);
    Advise(window);
    if (platform->setInputScope != nullptr)
    {
        (void)platform->setInputScope(window->hwnd, ScopeOf(purpose));
    }
    InputPane* pane = PaneOf(platform, window->hwnd);
    if (pane == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    boolean accepted = 0;
    HRESULT result =
        visible ? pane->v->tryShow(pane, &accepted) : pane->v->tryHide(pane, &accepted);
    (void)pane->v->Release(pane);
    return SUCCEEDED(result) && accepted ? mwin_outcomeDone : mwin_outcomeDenied;
}

void mwinWin32StopTouchKeyboard(mwinWin32Platform* platform)
{
    if (platform->msctf != nullptr)
    {
        FreeLibrary(platform->msctf);
    }
    if (platform->combase != nullptr)
    {
        FreeLibrary(platform->combase);
    }
    platform->msctf = nullptr;
    platform->combase = nullptr;
    platform->setInputScope = nullptr;
    platform->activate = nullptr;
    platform->keyboardTried = false;
}
