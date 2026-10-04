// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cards and their routes. A Device emits its active Route params when
// bound and again when one changes; each replaces the route of its
// profile device and direction, and the nodes on it take its form.

#include "pipewire_card.h"

#include "context.h"
#include "device.h"
#include "form.h"

#include <spa/param/route.h>
#include <spa/pod/parser.h>
#include <string.h>

// The port type in a route's info: a struct of a count and that many
// key and value strings.
static const char* PortType(const struct spa_pod* info)
{
    struct spa_pod_parser parser;
    struct spa_pod_frame frame;
    int32_t count = 0;
    spa_pod_parser_pod(&parser, info);
    if (spa_pod_parser_push_struct(&parser, &frame) < 0 ||
        spa_pod_parser_get_int(&parser, &count) < 0)
    {
        return nullptr;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        const char* key = nullptr;
        const char* value = nullptr;
        if (spa_pod_parser_get_string(&parser, &key) < 0 ||
            spa_pod_parser_get_string(&parser, &value) < 0)
        {
            return nullptr;
        }
        if (strcmp(key, "port.type") == 0)
        {
            return value;
        }
    }
    return nullptr;
}

bool maudPipewireReadRoute(const struct spa_pod* param, maudPipewireRoute* route)
{
    uint32_t direction = 0;
    int32_t device = 0;
    const struct spa_pod* info = nullptr;
    if (param == nullptr ||
        spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamRoute, nullptr, SPA_PARAM_ROUTE_direction,
                             SPA_POD_Id(&direction), SPA_PARAM_ROUTE_device, SPA_POD_Int(&device),
                             SPA_PARAM_ROUTE_info, SPA_POD_OPT_Pod(&info)) < 0)
    {
        return false;
    }
    maudDirection ours =
        direction == SPA_DIRECTION_OUTPUT ? maud_directionOutput : maud_directionInput;
    *route = (maudPipewireRoute){
        .device = device,
        .direction = ours,
        .form = info != nullptr ? maudFormOfName(PortType(info), ours) : maud_formUnknown,
    };
    return true;
}

bool maudPipewireStoreRoute(maudPipewireCard* card, const maudPipewireRoute* route)
{
    uint32_t at = 0;
    while (at < card->routeCount && (card->routes[at].device != route->device ||
                                     card->routes[at].direction != route->direction))
    {
        ++at;
    }
    if (at == MAUD_PIPEWIRE_CARD_ROUTES)
    {
        return false;
    }
    card->routes[at] = *route;
    card->routeCount += at == card->routeCount ? 1u : 0u;
    return true;
}

static const maudPipewireCard* CardOf(const maudPipewire* pipewire, uint32_t globalId)
{
    for (uint32_t i = 0; i < pipewire->cardCapacity; ++i)
    {
        if (pipewire->cards[i].used && pipewire->cards[i].globalId == globalId)
        {
            return &pipewire->cards[i];
        }
    }
    return nullptr;
}

maudDeviceForm maudPipewireNodeForm(const maudPipewire* pipewire, const maudPipewireNode* node)
{
    const maudPipewireCard* card = node->hasCard ? CardOf(pipewire, node->cardId) : nullptr;
    for (uint32_t i = 0; card != nullptr && i < card->routeCount; ++i)
    {
        const maudPipewireRoute* route = &card->routes[i];
        if (route->device == node->profileDevice && route->direction == node->direction &&
            route->form != maud_formUnknown)
        {
            return route->form;
        }
    }
    return node->factorForm;
}

// Gives every node of a card the form its routes now name.
static void Apply(maudPipewire* pipewire, uint32_t cardId)
{
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        const maudPipewireNode* node = &pipewire->nodes[i];
        maudDeviceSlot* slot = node->used && node->hasCard && node->cardId == cardId
                                   ? maudFindDevice(pipewire->context, node->device)
                                   : nullptr;
        if (slot != nullptr)
        {
            maudSetDeviceForm(pipewire->context, slot, maudPipewireNodeForm(pipewire, node));
        }
    }
}

static void OnRoute(void* data, int seq, uint32_t id, uint32_t index, uint32_t next,
                    const struct spa_pod* param)
{
    (void)seq;
    (void)index;
    (void)next;
    maudPipewireCard* card = data;
    maudPipewireRoute route;
    if (id == SPA_PARAM_Route && maudPipewireReadRoute(param, &route) &&
        maudPipewireStoreRoute(card, &route))
    {
        Apply(card->owner, card->globalId);
    }
}

static const struct pw_device_events s_cardEvents = {
    .version = PW_VERSION_DEVICE_EVENTS,
    .param = OnRoute,
};

void maudPipewireAddCard(maudPipewire* pipewire, uint32_t globalId)
{
    maudPipewireCard* card = nullptr;
    for (uint32_t i = 0; i < pipewire->cardCapacity && card == nullptr; ++i)
    {
        card = pipewire->cards[i].used ? nullptr : &pipewire->cards[i];
    }
    if (card == nullptr)
    {
        return;
    }
    *card = (maudPipewireCard){.owner = pipewire, .globalId = globalId, .used = true};
    card->proxy = pw_registry_bind(pipewire->connection.registry, globalId,
                                   PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
    if (card->proxy == nullptr)
    {
        *card = (maudPipewireCard){0};
        return;
    }
    struct pw_device* device = (struct pw_device*)card->proxy;
    pw_device_add_listener(device, &card->listener, &s_cardEvents, card);
    uint32_t ids[] = {SPA_PARAM_Route};
    pw_device_subscribe_params(device, ids, 1);
}

static void Forget(maudPipewire* pipewire, maudPipewireCard* card)
{
    spa_hook_remove(&card->listener);
    pipewire->api.proxyDestroy(card->proxy);
    *card = (maudPipewireCard){0};
}

bool maudPipewireRemoveCard(maudPipewire* pipewire, uint32_t globalId)
{
    maudPipewireCard* card = (maudPipewireCard*)CardOf(pipewire, globalId);
    if (card == nullptr)
    {
        return false;
    }
    Forget(pipewire, card);
    Apply(pipewire, globalId);
    return true;
}

void maudPipewireDropCards(maudPipewire* pipewire)
{
    for (uint32_t i = 0; i < pipewire->cardCapacity; ++i)
    {
        if (pipewire->cards[i].used)
        {
            Forget(pipewire, &pipewire->cards[i]);
        }
    }
}
