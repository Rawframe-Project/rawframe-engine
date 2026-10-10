#pragma once

// The views the process's local players see (ADR-0052, D367): each one's
// region of its window and its cameras' geometry, as the presentation last
// derived them from declared state (the camera on the player's entity, the
// game's layout), and the window's size in logical pixels as its program
// last heard. A client host lends one as `rawframe.view.player_views`; the
// scene tells each local player's perspective view and the canvas its
// orthographic one as they extract a frame, and a game's sample function
// reads its player's to turn the pointer into a ray or a World point.

#include "rawframe/composition/participant.h"
#include "rawframe/view/view.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace rawframe::view {

/// A view's region as fractions of its window's width and height from its
/// top left, its edges where they are drawn.
struct Region {
    float left = 0;
    float top = 0;
    float width = 1;
    float height = 1;
};

/// A player's view placed in the window: its top left in logical pixels
/// from the window's, its size, and its camera. The size is nought while
/// the window has none, which every verb refuses as `NoViewSize`.
template <typename Camera> struct Placed {
    float left = 0;
    float top = 0;
    ViewSize size;
    Camera camera;
};

class PlayerViews {
public:
    PlayerViews() = default;
    PlayerViews(const PlayerViews&) = delete;
    PlayerViews& operator=(const PlayerViews&) = delete;

    /// The program's side, once a frame: the window's size in logical
    /// pixels.
    void window(ViewSize size) noexcept {
        window_ = size;
    }
    /// The window's size in logical pixels as last told; nought before.
    [[nodiscard]] ViewSize window() const noexcept {
        return window_;
    }
    /// The program's side, once a frame: what of the window the platform
    /// covers, which the UI lays out inside (SPEC-0030's root input, D589).
    void safeArea(ViewInsets insets) noexcept {
        safeArea_ = insets;
    }
    /// What of the window is covered, as last told; nothing before.
    [[nodiscard]] ViewInsets safeArea() const noexcept {
        return safeArea_;
    }

    /// The presentation's side: `player`'s view through a camera of the
    /// kind as it is now, or none (its World or its entity is gone).
    void tell(std::size_t player, const Region& region, const Perspective& camera);
    void tell(std::size_t player, const Region& region, const Orthographic& camera);
    void forgetPerspective(std::size_t player) noexcept;
    void forgetOrthographic(std::size_t player) noexcept;

    /// `player`'s view through a camera of the kind, placed in the window;
    /// none before the presentation derived one, and none where nothing
    /// presents it (a bot, a server, a game that draws no such view).
    [[nodiscard]] std::optional<Placed<Perspective>> perspective(std::size_t player) const noexcept;
    [[nodiscard]] std::optional<Placed<Orthographic>> orthographic(std::size_t player) const noexcept;

private:
    template <typename Camera> struct Told {
        Region region;
        Camera camera;
    };

    ViewSize window_;
    ViewInsets safeArea_;
    std::vector<std::optional<Told<Perspective>>> perspectives_;
    std::vector<std::optional<Told<Orthographic>>> orthographics_;
};

inline constexpr composition::Capability<PlayerViews> kPlayerViews{"rawframe.view.player_views"};

} // namespace rawframe::view
