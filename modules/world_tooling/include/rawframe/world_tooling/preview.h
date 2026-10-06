#pragma once

// Where a tooling client's look goes (D432): a Runtime that shows a preview
// (a client with a window) lends one as `rawframe.tooling.previewer`, and
// the endpoint's `tooling.look` hands it the author's eye, target, and
// field of view. A dedicated server lends none; the tooling module itself
// knows no camera.

#include "rawframe/composition/participant.h"

#include <array>
#include <optional>

namespace rawframe::world_tooling {

/// An author's view: an eye and the point it looks at, in metres, and its
/// vertical field of view in degrees.
struct Look {
    std::array<double, 3> eye{};
    std::array<double, 3> target{};
    double fieldOfView = 60;
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
};

inline constexpr composition::Capability<Previewer> kPreviewer{"rawframe.tooling.previewer"};

} // namespace rawframe::world_tooling
