// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// PipeWire's cards: the Device objects whose Route params say which port
// each of their profile devices, and so each node, plays through or
// records from. A jack switch changes a Route, and the node's form.

#ifndef MAUL_AUDIO_SRC_PIPEWIRE_CARD_H
#define MAUL_AUDIO_SRC_PIPEWIRE_CARD_H

#include "pipewire_core.h"

// Reads a Route param into route; false when it is not one or lacks its
// direction or device. A route without a port type has an unknown form.
bool maudPipewireReadRoute(const struct spa_pod* param, maudPipewireRoute* route);

// Keeps a card's route: it replaces the one of its profile device and
// direction, or is added; false when the card holds as many as it can.
bool maudPipewireStoreRoute(maudPipewireCard* card, const maudPipewireRoute* route);

// Binds the Device global id and follows its routes.
void maudPipewireAddCard(maudPipewire* pipewire, uint32_t globalId);

// Forgets the card of global id, if it is one; false otherwise.
bool maudPipewireRemoveCard(maudPipewire* pipewire, uint32_t globalId);

// Forgets every card, as when the daemon goes away.
void maudPipewireDropCards(maudPipewire* pipewire);

// The form a node leads to: its card's route for its profile device,
// where the card has said, otherwise its own form factor.
maudDeviceForm maudPipewireNodeForm(const maudPipewire* pipewire, const maudPipewireNode* node);

#endif // MAUL_AUDIO_SRC_PIPEWIRE_CARD_H
