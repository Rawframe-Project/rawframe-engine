// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland focus requests.

#include "wayland_activation.h"

#include <stdlib.h>

static void Activate(mwinWaylandPlatform* platform, const char* token, struct wl_surface* surface)
{
    const mwinWaylandApi* api = &platform->api;
    api->proxyMarshalFlags((struct wl_proxy*)platform->activation, XDG_ACTIVATION_V1_ACTIVATE,
                           nullptr, mwinWlVersion(api, platform->activation), 0, token, surface);
}

void mwinWaylandDropActivation(mwinWaylandWindow* window)
{
    if (window->activation != nullptr)
    {
        (void)mwinWlRequest(&window->platform->api, window->activation,
                            XDG_ACTIVATION_TOKEN_V1_DESTROY, nullptr, WL_MARSHAL_FLAG_DESTROY);
        window->activation = nullptr;
    }
}

// The token came: the window is activated with it, and the request is
// done, as the compositor says nothing more.
static void OnDone(void* data, struct xdg_activation_token_v1* token, const char* text)
{
    (void)token;
    mwinWaylandWindow* window = data;
    mwinWaylandPlatform* platform = window->platform;
    Activate(platform, text, window->surface);
    mwinWaylandDropActivation(window);
    mwinComplete(platform->context, window->slot, window->activationRequest, mwin_outcomeDone);
}

static const struct xdg_activation_token_v1_listener s_tokenListener = {.done = OnDone};

int mwinWaylandRequestFocus(mwinWaylandWindow* window, uint32_t request)
{
    mwinWaylandPlatform* platform = window->platform;
    const mwinWaylandApi* api = &platform->api;
    if (platform->context->windows[window->slot].def.kind == mwin_windowTooltip)
    {
        return mwin_outcomeDenied;
    }
    if (platform->activation == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    // A newer request supersedes the one whose token is still coming.
    mwinWaylandDropActivation(window);
    struct xdg_activation_token_v1* token =
        mwinWlRequest(api, platform->activation, XDG_ACTIVATION_V1_GET_ACTIVATION_TOKEN,
                      &xdg_activation_token_v1_interface, 0);
    mwinWlListen(api, token, &s_tokenListener, window);
    uint32_t version = mwinWlVersion(api, token);
    if (platform->inputSerial != 0 && platform->seat != nullptr)
    {
        api->proxyMarshalFlags((struct wl_proxy*)token, XDG_ACTIVATION_TOKEN_V1_SET_SERIAL, nullptr,
                               version, 0, platform->inputSerial, platform->seat);
    }
    int32_t focus = platform->keyboard.focus;
    if (focus >= 0)
    {
        api->proxyMarshalFlags((struct wl_proxy*)token, XDG_ACTIVATION_TOKEN_V1_SET_SURFACE,
                               nullptr, version, 0, platform->windows[focus].surface);
    }
    (void)mwinWlRequest(api, token, XDG_ACTIVATION_TOKEN_V1_COMMIT, nullptr, 0);
    window->activation = token;
    window->activationRequest = request;
    return -1;
}

void mwinWaylandActivateAtStart(mwinWaylandWindow* window)
{
    const char* token = getenv("XDG_ACTIVATION_TOKEN");
    if (window->platform->activation != nullptr && token != nullptr && token[0] != '\0')
    {
        Activate(window->platform, token, window->surface);
        (void)unsetenv("XDG_ACTIVATION_TOKEN");
    }
}
