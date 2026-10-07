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

#include <cstdint>
#include <optional>

namespace rawframe::view {

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
    void clicked(const Ray& ray) noexcept {
        lastClick_ = ray;
        ++clicks_;
    }
    /// How many presses there have been, so a reader tells a new one, and
    /// the last one's ray; none before the first.
    [[nodiscard]] std::uint64_t clicks() const noexcept {
        return clicks_;
    }
    [[nodiscard]] const std::optional<Ray>& lastClick() const noexcept {
        return lastClick_;
    }

private:
    std::optional<Perspective> view_;
    std::optional<Ray> lastClick_;
    std::uint64_t clicks_ = 0;
};

inline constexpr composition::Capability<PreviewCamera> kPreviewCamera{"rawframe.view.preview_camera"};

} // namespace rawframe::view
