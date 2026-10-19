#pragma once

// Where a tooling client's look goes (D432): a Runtime that shows a preview
// (a client with a window) lends one as `rawframe.tooling.previewer`, and
// the endpoint's `tooling.look` hands it the author's eye, target, and
// field of view. A dedicated server lends none; the tooling module itself
// knows no camera.

#include "rawframe/composition/participant.h"
#include "rawframe/world_tooling/window_input.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    /// Where the pointer is, as a ray the same way, while it is in the
    /// window (D468).
    bool pointing = false;
    std::array<double, 3> pointOrigin{};
    std::array<double, 3> pointToward{};
    /// The wheel's detents (toward the author positive) and the pixels the
    /// pointer was carried with the right button held, totals from the
    /// start (D469).
    double wheel = 0;
    std::array<double, 2> orbit{};
    /// The same with Shift held, for freelook: the drag's pixels and the
    /// wheel's detents (D472).
    std::array<double, 2> look{};
    double fly = 0;
    /// Where the preview's own camera was flown to (D913): how many times,
    /// and its eye and the point ahead it orbits about; nought before.
    std::uint64_t flights = 0;
    std::array<double, 3> flownEye{};
    std::array<double, 3> flownTarget{};
};

/// One step of the frame a preview last drew (D842): a pass by its
/// renderer's name, a mesh by its identity, a material by its place, and
/// how many instances.
struct DrawnStep {
    std::string pass;
    std::uint64_t mesh = 0;
    std::uint32_t material = 0;
    std::uint32_t instances = 0;
};

/// What the frame a preview last drew holds (D842): how many frames its
/// renderer has described, its figures by the renderer's names, its steps
/// in the order they were drawn, and the GPU's time for each of its passes
/// timed, in nanoseconds, where the device times them (D894).
struct DrawnFrame {
    std::uint64_t frames = 0;
    std::vector<std::pair<std::string, std::int64_t>> figures;
    std::vector<DrawnStep> steps;
    std::vector<std::pair<std::string, std::uint64_t>> passes;
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
    /// something there, or none (D464), its part `lit` drawn lit: 0 none, 1
    /// to 3 the X, Y, and Z axes, 4 the ring (D468); whether the Runtime
    /// shows a mark.
    virtual bool mark(const std::optional<std::array<double, 3>>& /*at*/, std::uint8_t /*lit*/) {
        return false;
    }
    /// Draws the paths a previewed scene's splines make, each its points in
    /// order, in place of those it drew (D891); whether the Runtime draws
    /// them.
    virtual bool paths(const std::vector<std::vector<std::array<double, 3>>>& /*paths*/) {
        return false;
    }
    /// Outlines what each author has chosen, each its color (red, green,
    /// blue from nought to one) and its boxes (a center and half the sides
    /// along x, y, and z), in place of those it outlined (D907); whether the
    /// Runtime outlines them.
    virtual bool
    outlines(const std::vector<std::pair<std::array<float, 3>, std::vector<std::array<double, 6>>>>& /*outlines*/) {
        return false;
    }
    /// Takes what an author did in an editor's viewport showing the
    /// preview as its own window's (D790), in order; whether the Runtime
    /// takes such input.
    virtual bool input(const std::vector<WindowInput>& /*events*/) {
        return false;
    }
    /// Leaves out the entities of `layers` (`rawframe.world.layer`) from
    /// what the preview draws, none for none (D791); whether the Runtime
    /// hides layers.
    virtual bool hide(const std::vector<std::uint32_t>& /*layers*/) {
        return false;
    }
    /// Splits the preview's window to show `others` beside its own look,
    /// at most three, none for the one look (D797); whether the Runtime
    /// splits its preview.
    virtual bool split(const std::vector<Look>& /*others*/) {
        return false;
    }
    /// Draws the overlays named `overlays` over the preview (D798), the
    /// names the Runtime's renderer owns, none for none; whether it knows
    /// them all and draws them.
    virtual bool overlay(const std::vector<std::string>& /*overlays*/) {
        return false;
    }
    /// Draws the preview in the view mode named `mode` (D794), the names
    /// the Runtime's renderer owns; whether it knows the name and draws so.
    virtual bool viewMode(std::string_view /*mode*/) {
        return false;
    }
    /// What the frame the preview last drew holds (D842), its steps only
    /// with `steps`; none before the first frame, or for a Runtime whose
    /// renderer says nothing.
    [[nodiscard]] virtual std::optional<DrawnFrame> drawn(bool /*steps*/) const {
        return std::nullopt;
    }
};

inline constexpr composition::Capability<Previewer> kPreviewer{"rawframe.tooling.previewer"};

} // namespace rawframe::world_tooling
