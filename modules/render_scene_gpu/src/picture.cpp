#include "picture.h"

#include "rawframe/texture/texture.h"
#include "tables.h"

#include <array>
#include <maul-rhi/encoder.h>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

} // namespace

std::shared_ptr<const texture::Texture> plainTable() {
    texture::Texture made{.format = texture::Format::Rgba16Float, .depth = 2};
    texture::Level level{.width = 2, .height = 2};
    for (std::uint32_t blue = 0; blue < 2; ++blue) {
        for (std::uint32_t green = 0; green < 2; ++green) {
            for (std::uint32_t red = 0; red < 2; ++red) {
                for (const std::uint32_t kChannel : {red, green, blue, 1U}) {
                    const std::uint16_t kHalf = texture::halfOf(static_cast<float>(kChannel));
                    level.bytes.push_back(static_cast<std::byte>(kHalf & 0xFFU));
                    level.bytes.push_back(static_cast<std::byte>(kHalf >> 8U));
                }
            }
        }
    }
    made.levels.push_back(std::move(level));
    return std::make_shared<const texture::Texture>(std::move(made));
}

PicturePass::PicturePass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status PicturePass::declare(const render_scene::SceneFrame& frame,
                                    bool made,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::size_t bloomLevels,
                                    mrhiResourceId table,
                                    bool tabled,
                                    bool gradedFirst,
                                    std::vector<mrhiAccess>& writes) {
    block_ = pictureOf(frame);
    table_ = table;
    width_ = width;
    height_ = height;
    gradedFirst_ = gradedFirst;
    block_.display[1] = tabled && frame.grading.enabled ? 1.0F : 0.0F;
    if (bloomLevels > 0) {
        block_.bloom = {frame.bloom.intensity, 1.0F / static_cast<float>(bloomLevels), 0, 0};
    }
    graded_ = {};
    if (gradedFirst_) {
        // The grade's pass does the bloom, the grade, and the table, and
        // the tonemapper's none of them.
        gradeBlock_ = block_;
        gradeBlock_.display[2] = 1;
        block_.power[3] = 0;
        block_.bloom = {};
        block_.display[1] = 0;
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = kSceneFormat;
        def.width = width;
        def.height = height;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, &graded_); kDeclared != mrhi_success) {
            return failed("the graded light could not be declared", kDeclared);
        }
    }
    for (const auto& [kMade, kWanted] : {std::pair{&blockResource_, true}, std::pair{&gradeResource_, gradedFirst_}}) {
        *kMade = {};
        if (!kWanted) {
            continue;
        }
        mrhiBufferDef blockDef = mrhiDefaultBufferDef();
        blockDef.size = sizeof(PictureBlock);
        if (mrhiDeclareBuffer(native_, &blockDef, kMade) != mrhi_success) {
            return failed("the picture's grade could not be declared", mrhi_errorCapacity);
        }
        writes.push_back(wholeOf(*kMade, mrhi_accessCopyDestination));
    }
    smoothed_ = frame.fxaa && made;
    return {};
}

bool PicturePass::smoothed() const noexcept {
    return smoothed_;
}

namespace {

/// A pass drawing every pixel of `into`, over whatever was there, reading
/// `reads`.
mrhiPassDef drawingInto(mrhiResourceId into, bool clears, const std::vector<mrhiAccess>& reads) {
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0].resource = into;
    def.colorTargets[0].load = clears ? mrhi_loadClear : mrhi_loadKeep;
    def.colorTargets[0].store = mrhi_storeKeep;
    def.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
    def.colorTargetCount = 1;
    def.neverCull = true;
    def.accesses = reads.data();
    def.accessCount = static_cast<std::uint32_t>(reads.size());
    return def;
}

} // namespace

result::Result<mrhiResourceId> PicturePass::addGrade(mrhiResourceId shown, mrhiResourceId spread) {
    shown_ = shown;
    gradeSpread_ = spread;
    if (!gradedFirst_) {
        return shown;
    }
    std::vector<mrhiAccess> reads = {wholeOf(shown, mrhi_accessSampled),
                                     wholeOf(gradeResource_, mrhi_accessUniform),
                                     wholeOf(table_, mrhi_accessSampled)};
    if (spread.index1 != 0) {
        reads.push_back(wholeOf(spread, mrhi_accessSampled));
    }
    mrhiPassDef def = drawingInto(graded_, false, reads);
    def.colorTargets[0].load = mrhi_loadDiscard;
    def.neverCull = false;
    if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &gradePass_); kAdded != mrhi_success) {
        return failed("the grade's pass could not be added", kAdded);
    }
    return graded_;
}

result::Status PicturePass::addTonemap(mrhiResourceId light, mrhiResourceId spread, mrhiResourceId into, bool clears) {
    light_ = light;
    spread_ = gradedFirst_ ? mrhiResourceId{} : spread;
    std::vector<mrhiAccess> reads = {wholeOf(light_, mrhi_accessSampled),
                                     wholeOf(blockResource_, mrhi_accessUniform),
                                     wholeOf(table_, mrhi_accessSampled)};
    if (spread_.index1 != 0) {
        reads.push_back(wholeOf(spread_, mrhi_accessSampled));
    }
    const mrhiPassDef kDef = drawingInto(into, clears, reads);
    if (const mrhiResult kAdded = mrhiAddPass(native_, &kDef, &tonemapPass_); kAdded != mrhi_success) {
        return failed("the picture's pass could not be added", kAdded);
    }
    return {};
}

result::Status PicturePass::addFxaa(mrhiResourceId display, mrhiResourceId into, bool clears) {
    display_ = display;
    const std::vector<mrhiAccess> kReads = {wholeOf(display_, mrhi_accessSampled)};
    const mrhiPassDef kDef = drawingInto(into, clears, kReads);
    if (const mrhiResult kAdded = mrhiAddPass(native_, &kDef, &fxaaPass_); kAdded != mrhi_success) {
        return failed("the FXAA pass could not be added", kAdded);
    }
    return {};
}

result::Status PicturePass::write(mrhiPassId upload) {
    if (mrhiWriteBuffer(native_, upload, blockResource_, 0, &block_, sizeof(PictureBlock)) != mrhi_success ||
        (gradedFirst_ &&
         mrhiWriteBuffer(native_, upload, gradeResource_, 0, &gradeBlock_, sizeof(PictureBlock)) != mrhi_success)) {
        return failed("the picture's grade could not be written", mrhi_errorCapacity);
    }
    return {};
}

namespace {

/// The picture's shader drawn in `pass` with `pipeline`: the light
/// `light`, the grade `block`, the bloom's `spread` (the light where there
/// is none, which the shader does not read then, D328), and the grading
/// table, a volume (D344).
result::Status drawPicture(mrhiDevice* native,
                           mrhiPassId pass,
                           mrhiGraphicsPipelineId pipeline,
                           const Pipelines& pipelines,
                           std::array<mrhiResourceId, 4> bound) {
    const auto& [kLight, kBlock, kSpread, kTable] = bound;
    std::array<mrhiBinding, 5> binding = {textureAt(0, kLight),
                                          bufferAt(1, kBlock, sizeof(PictureBlock)),
                                          textureAt(2, kSpread.index1 != 0 ? kSpread : kLight),
                                          samplerAt(3, pipelines.filteredSampler),
                                          textureAt(4, kTable)};
    binding[4].viewKind = mrhi_texture3d;
    if (mrhiBeginPass(native, pass) != mrhi_success ||
        mrhiSetGraphicsPipeline(native, pass, pipeline) != mrhi_success ||
        mrhiSetBindings(native, pass, 0, binding.data(), binding.size()) != mrhi_success ||
        mrhiDraw(native, pass, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native, pass) != mrhi_success) {
        return failed("the picture could not be drawn", mrhi_errorState);
    }
    return {};
}

} // namespace

result::Status PicturePass::recordGrade(const Pipelines& pipelines) {
    if (!gradedFirst_) {
        return {};
    }
    return drawPicture(
        native_, gradePass_, pipelines.grade.pipeline, pipelines, {shown_, gradeResource_, gradeSpread_, table_});
}

result::Status PicturePass::recordTonemap(const Pipelines& pipelines) {
    return drawPicture(
        native_, tonemapPass_, pipelines.tonemap.pipeline, pipelines, {light_, blockResource_, spread_, table_});
}

result::Status PicturePass::recordFxaa(const Pipelines& pipelines) {
    if (!smoothed_) {
        return {};
    }
    const std::array<mrhiBinding, 2> kDisplayBinding = {textureAt(0, display_),
                                                        samplerAt(1, pipelines.filteredSampler)};
    if (mrhiBeginPass(native_, fxaaPass_) != mrhi_success ||
        mrhiSetGraphicsPipeline(native_, fxaaPass_, pipelines.fxaa.pipeline) != mrhi_success ||
        mrhiSetBindings(native_, fxaaPass_, 0, kDisplayBinding.data(), kDisplayBinding.size()) != mrhi_success ||
        mrhiDraw(native_, fxaaPass_, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, fxaaPass_) != mrhi_success) {
        return failed("the picture could not be antialiased", mrhi_errorState);
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
