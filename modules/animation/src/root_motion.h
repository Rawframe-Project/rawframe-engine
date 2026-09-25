#pragma once

// Root motion (SPEC-0035): where a skeleton's root motion source puts the
// character at a clip's time, how it moved between two times, played
// across a loop's wraps, and moves composed and blended. A move is a
// Transform of unit scale, in the character's own frame as it was when the
// move began.

#include "rawframe/animation/clip.h"
#include "rawframe/animation/skeleton.h"

#include <cstddef>
#include <span>

namespace rawframe::animation {

/// A clip's root tracks, as its root motion source reads them.
struct RootTracks {
    /// The root's translation and rotation tracks; null when the clip has
    /// none.
    const Track* translation = nullptr;
    const Track* rotation = nullptr;
};

/// `a`, then `b` from where `a` left the character.
[[nodiscard]] Transform then(const Transform& a, const Transform& b) noexcept;

/// How the character moved over `delta` seconds from the playhead `from`
/// to where the playhead `landed` (what `advance` made of it), across
/// every wrap of a looping clip between.
[[nodiscard]] Transform motionOver(const Clip& clip,
                                   const RootTracks& tracks,
                                   const RootMotionSource& source,
                                   const Transform& rootBind,
                                   double from,
                                   double delta,
                                   double landed);

/// Moves weighed by `shares` (summing to one), as a blend weighs poses.
[[nodiscard]] Transform blendedMotion(std::span<const Transform* const> moves, std::span<const double> shares);

/// The root's turn about the source's axis, from its bind rotation.
[[nodiscard]] std::array<double, 4>
twistOf(const RootMotionSource& source, const std::array<double, 4>& rotation, const std::array<double, 4>& bind);

} // namespace rawframe::animation
