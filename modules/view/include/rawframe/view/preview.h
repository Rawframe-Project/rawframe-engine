#pragma once

// The camera a preview shows the World through (D432): an authoring
// client looks at a running game from where its author looks at the scene,
// in place of the first local player's own camera, which still says how
// the picture is exposed and graded. A client host lends one as
// `rawframe.view.preview_camera`; its tooling endpoint sets it, and the
// scene renderer reads it each frame.

#include "rawframe/composition/participant.h"
#include "rawframe/view/view.h"

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

private:
    std::optional<Perspective> view_;
};

inline constexpr composition::Capability<PreviewCamera> kPreviewCamera{"rawframe.view.preview_camera"};

} // namespace rawframe::view
