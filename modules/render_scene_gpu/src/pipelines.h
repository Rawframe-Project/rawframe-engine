#pragma once

#include "rawframe/material/material.h"
#include "rawframe/render/device.h"
#include "rawframe/result/result.h"

#include <array>
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

/// A pipeline asked of the device, graphics or compute, and whether it is
/// made.
struct Asked {
    mrhiGraphicsPipelineId pipeline{};
    mrhiComputePipelineId compute{};
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
    mrhiShaderId skyShader{};
    mrhiShaderId meterShader{};
    mrhiShaderId fxaaShader{};
    /// Compares a shadow map's depths, blending four (hardware 2x2 PCF).
    mrhiSamplerId shadowSampler{};
    /// Blends four texels, clamped at the edges: the picture before's
    /// (D291) and FXAA's taps (D296).
    mrhiSamplerId filteredSampler{};
    /// A material's texture's, by its declared filter and address (D309):
    /// at `samplerOf`.
    std::array<mrhiSamplerId, 4> materialSamplers{};
    /// The shadow maps' casters, the depth prepass, the lit models, the sky
    /// behind them, the metering's two steps (D293), the temporal pass, the
    /// picture, and FXAA over it (D296).
    Asked casting;
    Asked depth;
    Asked lit;
    /// The translucent models, blended over what is behind them (D305).
    Asked glass;
    Asked sky;
    Asked histogram;
    Asked adapt;
    Asked temporal;
    Asked tonemap;
    Asked fxaa;

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
    result::Status ask(const mrhiComputePipelineDef& def, Asked& asked);
};

/// Where a material's filter and address put its sampler among
/// `Pipelines::materialSamplers`.
[[nodiscard]] constexpr std::size_t samplerOf(material::Filter filter, material::Address address) noexcept {
    return (static_cast<std::size_t>(filter) * 2) + static_cast<std::size_t>(address);
}

/// Maul RHI's refusal, named.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome);

} // namespace rawframe::render_scene_gpu
