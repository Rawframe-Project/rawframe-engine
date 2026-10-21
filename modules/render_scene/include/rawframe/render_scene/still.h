#pragma once

// Whether a view's frames draw the same picture (D952), so a device that
// draws on its processor does not draw an editor's idle view sixty times a
// second: what the frames that join a picture say when they are unchanged
// (D494).

#include "rawframe/render_scene/scene.h"

#include <cstdint>
#include <optional>

namespace rawframe::render_scene {

/// Whether `frame`'s picture moves with time alone, so it is never the one
/// before: a model drawn with a program material (which may read the
/// World's time), an emitter's particles, or a post process. Trails and
/// beams are drawn from the frame's own points.
[[nodiscard]] bool movesWithTime(const SceneFrame& frame) noexcept;

/// A view's frames, each taken as it is drawn.
class Stillness {
public:
    /// Takes the next frame a view draws: whether its picture has stood
    /// long enough to be the last one's. Equal leaves out what changes
    /// with time alone (the World's time, the temporal jitter and its
    /// history, the metering's step, the particles' clock); it has stood
    /// one frame before counting, as many as a temporal resolve gathers
    /// where the frame resolves one, and three seconds' worth at sixty
    /// where its exposure adapts to what it sees.
    [[nodiscard]] bool still(const SceneFrame& frame);

    /// How many frames the last one's picture has stood.
    [[nodiscard]] std::uint64_t stood() const noexcept {
        return stood_;
    }

private:
    std::optional<SceneFrame> last_;
    std::uint64_t stood_ = 0;
};

} // namespace rawframe::render_scene
