#pragma once

// What a client's present systems draw to see what the game does, with
// `rawframe.debug`: lines, boxes, spheres and arrows in World meters, each
// for a tick, some seconds, or until cleared, on one of 32 channels. The
// presentation provides it; the scene draws it over the World. Development
// builds only: a shipping build's doors draw nothing. A dedicated server
// has none.

#include "rawframe/composition/participant.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawframe::world_kest {

/// A shape a present system drew: its kind; two points, a line's or an
/// arrow's ends, a box's least and greatest corners, or a sphere's center
/// and, first of `b`, its radius; its color, 0xRRGGBBAA sRGB with straight
/// alpha; its channel; and the last presented tick it shows on.
struct DebugShape {
    enum class Kind : std::uint8_t {
        Line,
        Box,
        Sphere,
        Arrow
    };
    Kind kind = Kind::Line;
    std::array<double, 3> a{};
    std::array<double, 3> b{};
    std::uint32_t color = 0xFFFFFFFF;
    std::uint32_t channel = 0;
    std::uint64_t until = 0;
};

/// The channels a shape may be on: 0 to 31.
inline constexpr std::uint32_t kDebugChannels = 32;
/// The shapes held at once (a limit point); past it a shape is left out
/// and counted.
inline constexpr std::size_t kMostDebugShapes = 1024;
/// A shape's `until` when it stays until its channel is cleared.
inline constexpr std::uint64_t kDebugForever = UINT64_MAX;

struct PresentedDrawings {
    std::vector<DebugShape> shapes;
    /// The presented tick the shapes are drawn for; a shape whose `until`
    /// is before it is gone.
    std::uint64_t tick = 0;
    /// Moved on by each change, so a reader draws again only what changed.
    std::uint64_t revision = 0;
    /// Shapes left out: past the most held, on no channel, or not finite.
    std::uint64_t leftOut = 0;

    /// The next tick: the shapes whose time is up gone.
    void advance();
    /// `shape` held from now, until its `until`; whether it was.
    bool add(const DebugShape& shape);
    /// Every shape on `channel` gone, or on every channel for
    /// `kDebugChannels` or past.
    void clear(std::uint32_t channel);
};

inline constexpr composition::Capability<PresentedDrawings> kPresentedDrawings{
    "rawframe.world_kest.presented_drawings"};

} // namespace rawframe::world_kest
