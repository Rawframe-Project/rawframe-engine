// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland outputs as monitors. An output's facts arrive one event at a
// time and hold together at its done event, which adds or changes its
// monitor. Wayland names no primary output, so none is primary. With
// the color manager, an output's image description gives its HDR facts
// (mwin-0036): its transfer function, its target luminances and the
// luminance of SDR white.

#ifndef MAUL_WINDOW_SRC_WAYLAND_OUTPUT_H
#define MAUL_WINDOW_SRC_WAYLAND_OUTPUT_H

#include "wayland.h"

// Binds an output the registry announced; one past the monitor limit is
// left unbound.
void mwinWaylandBindOutput(mwinWaylandPlatform* platform, uint32_t name, uint32_t version);

// A global went: when it was a bound output, removes its monitor and
// releases it.
void mwinWaylandRemoveOutput(mwinWaylandPlatform* platform, uint32_t name);

// The color manager came: each bound output's image description is
// watched for its HDR facts, as each output bound after.
void mwinWaylandWatchColors(mwinWaylandPlatform* platform);

// Releases every output, at the end.
void mwinWaylandReleaseOutputs(mwinWaylandPlatform* platform);

// The monitor slot of an output, or -1 for one without a monitor.
int32_t mwinWaylandMonitorOf(const mwinWaylandPlatform* platform, const struct wl_output* output);

#endif // MAUL_WINDOW_SRC_WAYLAND_OUTPUT_H
