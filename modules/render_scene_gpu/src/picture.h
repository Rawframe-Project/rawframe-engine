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
/// D295), the bloom's spread light mixed in first (D328); with FXAA, a
/// pass more over the tonemapped picture (D296). With post processes
/// before the tonemapper, graded first into a light of its own, which the
/// tonemapper takes once they are done with it (D350). Each step writes
/// where it is told: the frame's picture, or a picture between steps.
class PicturePass {
public:
    explicit PicturePass(mrhiDevice* native) noexcept;

    /// Joins the open frame, at `width` by `height`, with the bloom's
    /// `bloomLevels` (nought for none) and the grading `table` its colors
    /// are looked up in where `tabled` (the plain one otherwise, D344),
    /// graded first where `gradedFirst`: its grade declared, and what the
    /// upload pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::size_t bloomLevels,
                           mrhiResourceId table,
                           bool tabled,
                           bool gradedFirst,
                           std::vector<mrhiAccess>& writes);

    /// Whether the open frame is antialiased by FXAA.
    [[nodiscard]] bool smoothed() const noexcept;

    /// Graded first, the grade's pass, reading the light `shown` and the
    /// bloom's `spread` (none for none), into a light of its own, which it
    /// gives; `shown` otherwise, and no pass.
    result::Result<mrhiResourceId> addGrade(mrhiResourceId shown, mrhiResourceId spread);
    /// The tonemapper's pass, reading `light` (and the bloom's `spread`
    /// unless graded first), into `into`, cleared first when `clears`.
    result::Status addTonemap(mrhiResourceId light, mrhiResourceId spread, mrhiResourceId into, bool clears);
    /// FXAA's pass, reading the tonemapped `display` into `into`.
    result::Status addFxaa(mrhiResourceId display, mrhiResourceId into, bool clears);

    /// Its writes, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded, each where it falls in the chain.
    result::Status recordGrade(const Pipelines& pipelines);
    result::Status recordTonemap(const Pipelines& pipelines);
    result::Status recordFxaa(const Pipelines& pipelines);

private:
    mrhiDevice* native_ = nullptr;
    /// The tonemapper's grade, and, graded first, the grade's.
    PictureBlock block_;
    PictureBlock gradeBlock_;
    mrhiResourceId blockResource_{};
    mrhiResourceId gradeResource_{};
    bool smoothed_ = false;
    bool gradedFirst_ = false;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    mrhiResourceId graded_{};
    mrhiResourceId shown_{};
    mrhiResourceId light_{};
    mrhiResourceId spread_{};
    mrhiResourceId gradeSpread_{};
    mrhiResourceId display_{};
    mrhiResourceId table_{};
    mrhiPassId gradePass_{};
    mrhiPassId tonemapPass_{};
    mrhiPassId fxaaPass_{};
};

} // namespace rawframe::render_scene_gpu
