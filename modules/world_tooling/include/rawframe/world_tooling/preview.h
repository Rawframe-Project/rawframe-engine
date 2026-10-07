#pragma once

// Where a tooling client's look goes (D432): a Runtime that shows a preview
// (a client with a window) lends one as `rawframe.tooling.previewer`, and
// the endpoint's `tooling.look` hands it the author's eye, target, and
// field of view. A dedicated server lends none; the tooling module itself
// knows no camera.

#include "rawframe/composition/participant.h"

#include <array>
#include <cstdint>
#include <optional>

namespace rawframe::world_tooling {

/// An author's view: an eye and the point it looks at, in metres, and its
/// vertical field of view in degrees.
struct Look {
    std::array<double, 3> eye{};
    std::array<double, 3> target{};
    double fieldOfView = 60;
};

/// The presses an author made in a preview while it looked (D456): how many
/// so far, and the last one as a ray from where it starts toward a point
/// far past it, in metres.
struct Clicked {
    std::uint64_t count = 0;
    std::array<double, 3> origin{};
    std::array<double, 3> toward{};
    /// The window's modifier bits held with the press: Shift 1, Control 2
    /// (D463).
    std::uint16_t modifiers = 0;
    /// Which press was last let go (its count), and the ray where it was
    /// let go (D457); nought before any.
    std::uint64_t released = 0;
    std::array<double, 3> releaseOrigin{};
    std::array<double, 3> releaseToward{};
};

class Previewer {
public:
    Previewer() = default;
    Previewer(const Previewer&) = delete;
    Previewer& operator=(const Previewer&) = delete;
    virtual ~Previewer() = default;

    /// The preview looks as `look` says from now on, none giving the
    /// player's camera back; false, changing nothing, for a look it cannot
    /// take (an eye at its target).
    virtual bool look(const std::optional<Look>& look) = 0;
    /// How many presses there have been in the preview while it looked,
    /// and the last one's ray from its eye: its origin, and how far and
    /// which way it goes (D456); none before the first, or for a Runtime
    /// that keeps none.
    [[nodiscard]] virtual std::optional<Clicked> clicked() const {
        return std::nullopt;
    }
    /// Marks a point of the World in the preview, as an author chose
    /// something there, or none (D464); whether the Runtime shows a mark.
    virtual bool mark(const std::optional<std::array<double, 3>>& /*at*/) {
        return false;
    }
};

inline constexpr composition::Capability<Previewer> kPreviewer{"rawframe.tooling.previewer"};

} // namespace rawframe::world_tooling
