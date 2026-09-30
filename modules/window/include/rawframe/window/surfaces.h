#pragma once

// The seam's window side (SPEC-0025, D280): what the device side reads of
// the program's windows once a frame, at SPEC-0024's `prepare`, and each
// surface generation's handle bundle, given once. The program that runs
// the windows updates it each frame before its Host runs and lends it to
// the Host's participants as `rawframe.window.surfaces`; nothing calls
// into device code when a window changes.

#include "rawframe/composition/participant.h"
#include "rawframe/window/events.h"
#include "rawframe/window/handles.h"
#include "rawframe/window/windows.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::window {

/// One window's drawable area as the program was last told of it.
struct SurfaceState {
    WindowId window;
    /// Its surface generation now; nought while it has none: before it is
    /// made, and between SurfaceLost and SurfaceRestored.
    std::uint32_t generation = 0;
    PixelSize pixelSize;
    /// Hidden, minimized, or without size: nothing to present to.
    bool occluded = false;
};

class Surfaces {
public:
    Surfaces() = default;
    Surfaces(const Surfaces&) = delete;
    Surfaces& operator=(const Surfaces&) = delete;

    /// The windows watched, in the order they were first watched.
    [[nodiscard]] std::span<const SurfaceState> states() const noexcept {
        return states_;
    }

    /// `window`'s handle bundle for its current surface generation, taken:
    /// each generation's is given once, to the one device side that makes
    /// its surface from it. None after, while the window has no surface,
    /// and for a window not watched.
    [[nodiscard]] std::optional<HandleBundle> take(WindowId window);

    /// The program's side: `window` is watched from now on.
    void watch(WindowId window);

    /// The program's side, once a frame before its Host runs: each watched
    /// window's state read in, and a new surface generation's handles held
    /// for `take`. A window that no longer exists is no longer watched.
    void update(const Windows& windows);

private:
    std::vector<SurfaceState> states_;
    /// Beside each state: the bundle not yet taken, and the generation
    /// whose handles were last read.
    std::vector<std::optional<HandleBundle>> pending_;
    std::vector<std::uint32_t> read_;
};

inline constexpr composition::Capability<Surfaces> kSurfaces{"rawframe.window.surfaces"};

} // namespace rawframe::window
