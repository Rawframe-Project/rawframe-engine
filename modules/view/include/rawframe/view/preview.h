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

/// A part of a mark: none, an axis (X, Y, Z), or the ring (D468).
enum class MarkPart : std::uint8_t {
    None,
    X,
    Y,
    Z,
    Ring
};

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
    /// A point the preview marks, where an author chose something (D464),
    /// drawn as three axes there; none for none. Counted, so a reader tells
    /// a new mark.
    void mark(const std::optional<std::array<double, 3>>& at, MarkPart lit = MarkPart::None) noexcept {
        marked_ = at;
        lit_ = lit;
        ++marks_;
    }
    /// The part of the mark drawn lit, the handle the pointer is over
    /// (D468).
    [[nodiscard]] MarkPart lit() const noexcept {
        return lit_;
    }
    [[nodiscard]] const std::optional<std::array<double, 3>>& marked() const noexcept {
        return marked_;
    }
    [[nodiscard]] std::uint64_t marks() const noexcept {
        return marks_;
    }

    /// Where the pointer is while the preview looks, as its ray, none once
    /// it left the window (D468): the handle under it is drawn lit.
    void pointed(const std::optional<Ray>& ray) noexcept {
        pointing_ = ray;
    }
    [[nodiscard]] const std::optional<Ray>& pointing() const noexcept {
        return pointing_;
    }

    /// The wheel turned while the preview looks, detents toward the user
    /// positive, and the pointer carried with the right button held, in
    /// pixels: each kept as a total from the start, so a reader tells what
    /// moved since it last read (D469).
    void wheeled(double detents) noexcept {
        wheel_ += detents;
    }
    void orbited(double x, double y) noexcept {
        orbit_[0] += x;
        orbit_[1] += y;
    }
    [[nodiscard]] double wheel() const noexcept {
        return wheel_;
    }
    [[nodiscard]] const std::array<double, 2>& orbit() const noexcept {
        return orbit_;
    }
    /// The same with Shift held, freelook's (D472): a drag turns the view
    /// about its eye, the wheel flies along it. Totals likewise.
    void looked(double x, double y) noexcept {
        look_[0] += x;
        look_[1] += y;
    }
    void flown(double detents) noexcept {
        fly_ += detents;
    }
    [[nodiscard]] const std::array<double, 2>& look() const noexcept {
        return look_;
    }
    [[nodiscard]] double fly() const noexcept {
        return fly_;
    }

    /// A press in the window while the preview looks, as its ray.
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
    MarkPart lit_ = MarkPart::None;
    std::optional<Ray> pointing_;
    double wheel_ = 0;
    std::array<double, 2> orbit_{};
    std::array<double, 2> look_{};
    double fly_ = 0;
    std::optional<Ray> lastRelease_;
    std::uint64_t releases_ = 0;
};

inline constexpr composition::Capability<PreviewCamera> kPreviewCamera{"rawframe.view.preview_camera"};

} // namespace rawframe::view
