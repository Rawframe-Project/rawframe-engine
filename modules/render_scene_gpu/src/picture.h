#pragma once

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <maul-rhi/frame.h>
#include <memory>
#include <vector>

namespace rawframe::texture {
struct Texture;
} // namespace rawframe::texture

namespace rawframe::render_scene_gpu {

/// The id the plain grading table is held as among the frame's textures:
/// bound where the frame looks up none, since the table's slot takes a
/// volume (D344).
inline constexpr std::uint64_t kNoTable = 0x6b1e94d2c07a35f8ULL;

/// A grading table that changes nothing: two texels a side, each its own
/// coordinates.
[[nodiscard]] std::shared_ptr<const texture::Texture> plainTable();

/// The frame's picture: the light it shows graded and tonemapped (D294,
/// D295), the bloom's spread light mixed in first (D328), into the frame's
/// picture; or, with FXAA, into a picture of its own that FXAA then reads
/// into the frame's (D296).
class PicturePass {
public:
    explicit PicturePass(mrhiDevice* native) noexcept;

    /// Joins the open frame, at `width` by `height`, with the bloom's
    /// `bloomLevels` (nought for none) and the grading `table` its colors
    /// are looked up in where `tabled` (the plain one otherwise, D344): its
    /// grade and, with FXAA, its own picture declared, and what the upload
    /// pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::size_t bloomLevels,
                           mrhiResourceId table,
                           bool tabled,
                           std::vector<mrhiAccess>& writes);

    /// Whether the open frame is antialiased by FXAA.
    [[nodiscard]] bool smoothed() const noexcept;

    /// Its passes, reading the light `shown` and the bloom's `spread` (none
    /// for none), into the frame's `picture`, cleared first when `clears`.
    result::Status addPasses(mrhiResourceId shown, mrhiResourceId spread, mrhiResourceId picture, bool clears);

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded.
    result::Status record(const Pipelines& pipelines);

private:
    mrhiDevice* native_ = nullptr;
    PictureBlock block_;
    mrhiResourceId blockResource_{};
    bool smoothed_ = false;
    mrhiResourceId display_{};
    mrhiResourceId shown_{};
    mrhiResourceId spread_{};
    mrhiResourceId table_{};
    mrhiPassId tonemapPass_{};
    mrhiPassId fxaaPass_{};
};

} // namespace rawframe::render_scene_gpu
