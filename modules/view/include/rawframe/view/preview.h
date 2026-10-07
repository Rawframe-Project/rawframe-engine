#pragma once

// The camera a preview shows the World through (D432): an authoring
// client looks at a running game from where its author looks at the scene,
// in place of the first local player's own camera, which still says how
// the picture is exposed and graded. A client host lends one as
// `rawframe.view.preview_camera`; its tooling endpoint sets it, and the
// scene renderer reads it each frame. A press in the window while it looks
// is kept as the ray it makes, which the tooling endpoint tells the author
// (D456), so what they clicked can be found.

#include "rawframe/composition/participant.h"
#include "rawframe/view/view.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rawframe::view {

/// The ring a mark is drawn with about its point on the level plane, its
/// turn's handle (D467): its radius in meters, past the axes' meter, and
/// the pieces it is drawn in.
inline constexpr double kMarkRingRadius = 1.25;
inline constexpr std::size_t kMarkRingPieces = 48;

class PreviewCamera {
public:
    PreviewCamera() = default;
    PreviewCamera(const PreviewCamera&) = delete;
    PreviewCamera& operator=(const PreviewCamera&) = delete;

    /// Looks through `view` from now on; none gives the player's camera
    /// back.
    void look(std::optional<Perspective> view) noexcept {
        view_ = view;
    }
    [[nodiscard]] const std::optional<Perspective>& looking() const noexcept {
        return view_;
    }
    /// A press in the window while the preview looks, as its ray.
    /// A point the preview marks, where an author chose something (D464),
    /// drawn as three axes there; none for none. Counted, so a reader tells
    /// a new mark.
    void mark(const std::optional<std::array<double, 3>>& at) noexcept {
        marked_ = at;
        ++marks_;
    }
    [[nodiscard]] const std::optional<std::array<double, 3>>& marked() const noexcept {
        return marked_;
    }
    [[nodiscard]] std::uint64_t marks() const noexcept {
        return marks_;
    }

    /// `modifiers` are the window's modifier bits held with the press: a
    /// drag with Shift moves height, with Control turns (D463).
    void clicked(const Ray& ray, std::uint16_t modifiers = 0) noexcept {
        lastClick_ = ray;
        clickModifiers_ = modifiers;
        ++clicks_;
    }
    [[nodiscard]] std::uint16_t clickModifiers() const noexcept {
        return clickModifiers_;
    }
    /// How many presses there have been, so a reader tells a new one, and
    /// the last one's ray; none before the first.
    [[nodiscard]] std::uint64_t clicks() const noexcept {
        return clicks_;
    }
    [[nodiscard]] const std::optional<Ray>& lastClick() const noexcept {
        return lastClick_;
    }
    /// The press let go while the preview looks, as the ray where it was
    /// let go (D457): with the press's, a drag.
    void released(const Ray& ray) noexcept {
        lastRelease_ = ray;
        releases_ = clicks_;
    }
    /// Which press was last let go (its count), and where; none before.
    [[nodiscard]] std::uint64_t releases() const noexcept {
        return releases_;
    }
    [[nodiscard]] const std::optional<Ray>& lastRelease() const noexcept {
        return lastRelease_;
    }

private:
    std::optional<Perspective> view_;
    std::optional<Ray> lastClick_;
    std::uint64_t clicks_ = 0;
    std::uint16_t clickModifiers_ = 0;
    std::optional<std::array<double, 3>> marked_;
    std::uint64_t marks_ = 0;
    std::optional<Ray> lastRelease_;
    std::uint64_t releases_ = 0;
};

inline constexpr composition::Capability<PreviewCamera> kPreviewCamera{"rawframe.view.preview_camera"};

} // namespace rawframe::view
