#pragma once

// The camera a preview shows the World through (D432): an authoring
// client looks at a running game from where its author looks at the scene,
// in place of the first local player's own camera, which still says how
// the picture is exposed and graded. A client host lends one as
// `rawframe.view.preview_camera`; its tooling endpoint sets it, and the
// scene renderer reads it each frame. A press in the window while it looks
// is kept as the ray it makes, which the tooling endpoint tells the author
// (D456), so what they clicked can be found. The scene renderer tells it
// what each frame it drew holds (D842).

#include "rawframe/composition/participant.h"
#include "rawframe/view/players.h"
#include "rawframe/view/view.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::view {

/// The ring a mark is drawn with about its point on the level plane, its
/// turn's handle (D467): its radius in meters, past the axes' meter, and
/// the pieces it is drawn in.
inline constexpr double kMarkRingRadius = 1.25;
inline constexpr std::size_t kMarkRingPieces = 48;

/// How a preview's scene is drawn (D794): lit as the game is, its
/// surfaces' own colors unlit, or their normals as colors. Named by
/// `viewModeNamed`'s names, which are what a tool sends.
enum class ViewMode : std::uint8_t {
    Lit,
    Unlit,
    Normals
};

/// The view mode named `name` ("lit", "unlit", "normals"), none for
/// another.
[[nodiscard]] std::optional<ViewMode> viewModeNamed(std::string_view name) noexcept;

/// Where view `at` of a preview split `count` ways lies in its window
/// (D797): one fills it, two stand side by side, three are its left half
/// and the right half's top and bottom, four its quarters from the top
/// left across. The first is where the preview's own look is drawn,
/// picked in, and moved.
[[nodiscard]] Region previewRegion(std::size_t count, std::size_t at) noexcept;

/// What a preview draws over its scene (D798), a bit each: each body's
/// collider, each light's reach. Named by `overlayNamed`'s names, which
/// are what a tool sends.
enum class Overlay : std::uint8_t {
    Colliders = 1,
    Lights = 2
};

/// The overlay named `name` ("colliders", "lights"), none for another.
[[nodiscard]] std::optional<Overlay> overlayNamed(std::string_view name) noexcept;

/// One figure of the frame a preview last drew (D842): what the scene
/// renderer counted, by a name it owns ("draws", "lights_culled").
struct FrameFigure {
    std::string_view name;
    std::int64_t value = 0;
};

/// One step of the frame a preview last drew (D842): a pass by the name
/// the scene renderer owns ("opaque", "shadow_cascade"), and what it drew
/// there: a mesh by its identity, a material by its place among the
/// frame's, and how many instances, in the order they were drawn.
struct FrameStep {
    std::string_view pass;
    std::uint64_t mesh = 0;
    std::uint32_t material = 0;
    std::uint32_t instances = 0;
};

/// The most steps a frame's description keeps: a capture of a crowded
/// frame says how many it left out.
inline constexpr std::size_t kMostFrameSteps = 4096;

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

    /// The layers whose entities the preview leaves out (D791), as
    /// `rawframe.world.layer` numbers them; none hides nothing.
    void hide(std::vector<std::uint32_t> layers) {
        hidden_ = std::move(layers);
    }
    [[nodiscard]] const std::vector<std::uint32_t>& hidden() const noexcept {
        return hidden_;
    }

    /// The looks the preview's window is split to show beside its own,
    /// as many as it allows, drawn in `previewRegion`'s places after the
    /// first (D797); none shows the one look in the whole window.
    void split(std::vector<Perspective> others) {
        others_ = std::move(others);
        if (others_.size() + 1 > views_) {
            others_.resize(views_ - 1);
        }
    }
    /// The views its window may be split into, its host's to allow before
    /// the renderer starts, which places their regions then (D797): one,
    /// none split, unless the window's settings say more.
    void allow(std::size_t views) noexcept {
        views_ = std::max<std::size_t>(views, 1);
    }
    [[nodiscard]] std::size_t views() const noexcept {
        return views_;
    }
    [[nodiscard]] const std::vector<Perspective>& others() const noexcept {
        return others_;
    }

    /// What the preview draws over its scene while it looks (D798), as
    /// `Overlay` bits.
    void overlay(std::uint8_t overlays) noexcept {
        overlays_ = overlays;
    }
    [[nodiscard]] std::uint8_t overlays() const noexcept {
        return overlays_;
    }

    /// How the preview's scene is drawn while it looks (D794).
    void show(ViewMode mode) noexcept {
        mode_ = mode;
    }
    [[nodiscard]] ViewMode mode() const noexcept {
        return mode_;
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

    /// The scene renderer's description of the frame it last drew from
    /// this preview (D842): its figures and its steps, at most
    /// `kMostFrameSteps`, each in its order; and how many frames it has
    /// described, none before the first.
    void described(std::span<const FrameFigure> figures, std::span<const FrameStep> steps) {
        figures_.assign(figures.begin(), figures.end());
        steps_.assign(steps.begin(),
                      steps.begin() + static_cast<std::ptrdiff_t>(std::min(steps.size(), kMostFrameSteps)));
        ++described_;
    }
    [[nodiscard]] std::span<const FrameFigure> figures() const noexcept {
        return figures_;
    }
    [[nodiscard]] std::span<const FrameStep> steps() const noexcept {
        return steps_;
    }
    [[nodiscard]] std::uint64_t descriptions() const noexcept {
        return described_;
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
    std::vector<std::uint32_t> hidden_;
    ViewMode mode_ = ViewMode::Lit;
    std::uint8_t overlays_ = 0;
    std::vector<Perspective> others_;
    std::size_t views_ = 1;
    std::vector<FrameFigure> figures_;
    std::vector<FrameStep> steps_;
    std::uint64_t described_ = 0;
};

inline constexpr composition::Capability<PreviewCamera> kPreviewCamera{"rawframe.view.preview_camera"};

} // namespace rawframe::view
