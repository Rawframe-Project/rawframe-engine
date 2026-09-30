#pragma once

#include "rawframe/render/device.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <maul-rhi/pipeline.h>
#include <maul-rhi/resources.h>
#include <maul-rhi/shader.h>
#include <span>
#include <string_view>

namespace rawframe::render_scene_gpu {

/// The shadow maps' depth: the sun's cascades' (D289) and the punctual
/// lights' atlas (D292).
constexpr mrhiFormat kShadowFormat = mrhi_formatDepth32Float;

constexpr mrhiFormat kSceneFormat = mrhi_formatRgba16Float;
/// Where each texel's point moved since the frame before (D291).
constexpr mrhiFormat kMotionFormat = mrhi_formatRg16Float;
constexpr mrhiFormat kDepthFormat = mrhi_formatDepth32Float;
/// The frame's picture, as `render` declares it.
constexpr mrhiFormat kPictureFormat = mrhi_formatRgba8UnormSrgb;

/// A pipeline asked of the device, and whether it is made.
struct Asked {
    mrhiGraphicsPipelineId pipeline{};
    std::uint64_t request = 0;
    bool ready = false;
};

/// The scene's shaders, samplers, and pipelines (D284 to D292): asked of
/// the device at once, made as it answers, and destroyed with it.
struct Pipelines {
    render::Device* device = nullptr;
    mrhiDevice* native = nullptr;
    mrhiShaderId sceneShader{};
    mrhiShaderId tonemapShader{};
    mrhiShaderId shadowShader{};
    mrhiShaderId temporalShader{};
    /// Compares a shadow map's depths, blending four (hardware 2x2 PCF).
    mrhiSamplerId shadowSampler{};
    /// Blends four texels of the picture before.
    mrhiSamplerId historySampler{};
    /// The shadow maps' casters, the depth prepass, the lit models, the
    /// temporal pass, and the picture.
    Asked casting;
    Asked depth;
    Asked lit;
    Asked temporal;
    Asked tonemap;

    Pipelines() = default;
    Pipelines(const Pipelines&) = delete;
    Pipelines& operator=(const Pipelines&) = delete;
    ~Pipelines();

    /// Every shader and sampler made, every pipeline asked for.
    result::Status make();

    /// Whether every pipeline is made; an error if one could not be.
    result::Result<bool> ready();

private:
    result::Status makeShader(std::span<const std::uint8_t> container, mrhiShaderId& shader);
    result::Status ask(const mrhiGraphicsPipelineDef& def, Asked& asked);
};

/// Maul RHI's refusal, named.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome);

} // namespace rawframe::render_scene_gpu
