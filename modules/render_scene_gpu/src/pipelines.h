#pragma once

#include "rawframe/material/material.h"
#include "rawframe/render/device.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <maul-rhi/pipeline.h>
#include <maul-rhi/resources.h>
#include <maul-rhi/shader.h>
#include <memory>
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
/// Each point's normal and roughness, the prepass's auxiliary target when
/// a screen-space effect asks for it (ADR-0051, D327).
constexpr mrhiFormat kSurfaceFormat = mrhi_formatRgba16Float;
/// What of the light from all around reaches each point (D327).
constexpr mrhiFormat kAmbientFormat = mrhi_formatR8Unorm;
/// What each point reflects of the picture before, and how much (D331).
constexpr mrhiFormat kReflectionFormat = mrhi_formatRgba16Float;
/// The decals' normals' atlas (D342): linear, as normal textures are.
constexpr mrhiFormat kNormalsFormat = mrhi_formatRgba8Unorm;
/// The reflection probes' atlas (D340), as the pictures it holds are.
constexpr mrhiFormat kProbeFormat = mrhi_formatRgba16Float;
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

/// The models' passes' pipelines multisampled (D343): the prepass's, whole
/// and masked, with and without the surfaces; the lit, masked, and
/// translucent models', plain and under decals; the sky's; and the resolve
/// of the prepass's depth to one sample.
struct Multisampled {
    Asked depth;
    Asked cutout;
    Asked surfaces;
    Asked cutSurfaces;
    Asked lit;
    Asked maskedLit;
    Asked glass;
    Asked litDecaled;
    Asked maskedLitDecaled;
    Asked glassDecaled;
    Asked sky;
    Asked resolveDepth;
};

/// The models' pipelines a material's program draws with, by what they
/// draw: lit, the opaque, the masked (D310), and the translucent (D305);
/// in the prepass, the masked cut, and each point's surface, whole and
/// masked (D327, D487).
enum class Shade : std::uint8_t {
    Lit,
    Masked,
    Glass,
    Cut,
    Surfaces,
    CutSurfaces
};

/// A material's own program's pipelines (D485, D487): the engine's own
/// models' but for the shader, by what they draw; lit, plain and under
/// decals; each single- and multisampled; each asked for when a frame first
/// wants it (`variantOf`); refused where its shader or a pipeline could not
/// be made, its models then drawn by the engine's own.
struct ProgramShading {
    std::shared_ptr<const material::ProgramMaterial> program;
    mrhiShaderId shader{};
    std::array<Asked, 18> variants{};
    std::array<bool, 18> asked{};
    bool refused = false;
};

/// Where a program's pipeline for `shade` lies among its variants: the lit
/// twelve, then the prepass's six, which no decal changes.
[[nodiscard]] constexpr std::size_t variantOf(Shade shade, bool decaled, bool multisampled) noexcept {
    const auto kShade = static_cast<std::size_t>(shade);
    if (kShade >= 3) {
        return 12U + (kShade - 3U) + (multisampled ? 3U : 0U);
    }
    return kShade + (decaled ? 3U : 0U) + (multisampled ? 6U : 0U);
}

/// The optional effects whose pipelines are asked for only when a view
/// first wants them (D337): the prepass's surfaces target, which the
/// ambient occlusion and the reflections read, and each effect.
enum class Effect : std::uint8_t {
    Surfaces,
    Occlusion,
    Reflections,
    MotionBlur,
    DepthOfField,
    Bloom,
    Fxaa,
    ContactShadows,
    Decals,
    Probes,
    Multisampled,
    PostProcess
};
inline constexpr std::size_t kEffects = 12;

/// The scene's shaders, samplers, and pipelines (D284 to D292): what every
/// frame draws with asked of the device at once, each effect's when a view
/// first wants it (D337), made as the device answers, and destroyed with
/// it.
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
    mrhiShaderId occlusionShader{};
    mrhiShaderId bloomShader{};
    mrhiShaderId reflectShader{};
    mrhiShaderId motionShader{};
    mrhiShaderId focusShader{};
    mrhiShaderId contactShader{};
    mrhiShaderId decalShader{};
    mrhiShaderId probeShader{};
    mrhiShaderId resolveShader{};
    mrhiShaderId postShader{};
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
    /// The masked casters, cut where their opacity falls below their
    /// cutoff (D310).
    Asked cutCasting;
    Asked depth;
    /// The masked models' depth, cut where their opacity falls below their
    /// cutoff (D310).
    Asked cutout;
    /// The prepass leaving each point's surface too, for the ambient
    /// occlusion (D327), whole and masked.
    Asked surfaces;
    Asked cutSurfaces;
    /// The ambient occlusion, and its blur (D327).
    Asked occlude;
    Asked blurOcclusion;
    /// The screen-space reflections (D331).
    Asked march;
    /// The motion blur's tiles, their neighbors, and its gathering (D334).
    Asked motionTiles;
    Asked motionNeighbors;
    Asked motionGather;
    /// The depth of field's halving, its bokeh, and its blend (D336).
    Asked focusPrefilter;
    Asked focusBokeh;
    Asked focusCombine;
    /// The contact shadows (D338).
    Asked contactShade;
    /// A decal's texture drawn into the colors' atlas, its normal texture
    /// into the normals' (D342), and the lit, masked, and translucent
    /// models under the frame's decals (D339).
    Asked decalFill;
    Asked decalNormalFill;
    Asked litDecaled;
    Asked maskedLitDecaled;
    Asked glassDecaled;
    /// A reflection probe's picture drawn into the atlas (D340).
    Asked probeFill;
    Asked lit;
    /// The masked models lit only where their depth is the prepass's
    /// (D310).
    Asked maskedLit;
    /// The translucent models, blended over what is behind them (D305).
    Asked glass;
    Asked sky;
    Asked histogram;
    Asked adapt;
    Asked temporal;
    Asked tonemap;
    Asked fxaa;
    /// The post processes (D350): in scene-linear light, in the picture's
    /// display-referred light, and the picture graded only, the light the
    /// post processes before the tonemapper take.
    Asked postLinear;
    Asked postDisplay;
    Asked grade;
    /// The bloom's first halving, its others, and its doublings, added to
    /// the level above (D328).
    Asked bloomFirst;
    Asked bloomDown;
    Asked bloomUp;
    /// The multisampled models' passes (D343), and the samples a pixel they
    /// take: set once, before they are first asked for; nought until then.
    Multisampled multisampled;
    std::uint32_t samples = 0;
    /// Every material's own program a frame has named, by its program
    /// (D485), held so no other takes its place while its pipelines live.
    std::map<const material::ProgramMaterial*, ProgramShading> programs;

    Pipelines() = default;
    Pipelines(const Pipelines&) = delete;
    Pipelines& operator=(const Pipelines&) = delete;
    ~Pipelines();

    /// Every sampler made, and every shader and pipeline every frame draws
    /// with asked for.
    result::Status make();

    /// Whether every pipeline every frame draws with is made; an error if
    /// one could not be.
    result::Result<bool> ready();

    /// Whether `effect`'s pipelines are made: asked for the first time a
    /// frame wants it, so the frames until the device answers go without
    /// it; an error if one could not be made.
    result::Result<bool> wanted(Effect effect);

    /// Each of `named`'s programs' lit pipelines a frame draws with asked
    /// for, plain and, where the frame wants them, under decals and
    /// multisampled (`sampled`); those asked before polled (D485). A program refused
    /// is refused for good, and is no frame's failure.
    void
    wantPrograms(std::span<const std::shared_ptr<const material::ProgramMaterial>> named, bool decaled, bool sampled);

    /// The pipeline `program` draws `shade` with, made; none before, and
    /// for a program refused.
    [[nodiscard]] const Asked*
    programPipeline(const material::ProgramMaterial* program, Shade shade, bool decaled, bool sampled) const;

private:
    result::Status askFor(Effect effect);
    result::Result<bool> answered(std::initializer_list<Asked*> pipelines);
    result::Status makeShader(std::span<const std::uint8_t> container, mrhiShaderId& shader);
    result::Status ask(const mrhiGraphicsPipelineDef& def, Asked& asked);
    result::Status ask(const mrhiComputePipelineDef& def, Asked& asked);
    /// A pipeline the device refused, named by its label, with Maul RHI's
    /// diagnostic when it recorded one.
    result::Status refused(mrhiResult outcome, const char* label, std::size_t labelLength);

    /// Which effects have been asked for, and the prepass's and the
    /// picture's pipelines, which the surfaces and FXAA are made from.
    std::array<bool, kEffects> asked_{};
    mrhiGraphicsPipelineDef prepass_{};
    mrhiGraphicsPipelineDef picture_{};
    /// The lit, masked, and translucent models' pipelines, which their
    /// decaled twins are made from; the masked prepass's and the sky's,
    /// which the multisampled ones are made from too.
    std::array<mrhiGraphicsPipelineDef, 3> shading_{};
    mrhiGraphicsPipelineDef cut_{};
    mrhiGraphicsPipelineDef sky_{};
};

/// Where a material's filter and address put its sampler among
/// `Pipelines::materialSamplers`.
[[nodiscard]] constexpr std::size_t samplerOf(material::Filter filter, material::Address address) noexcept {
    return (static_cast<std::size_t>(filter) * 2) + static_cast<std::size_t>(address);
}

/// Maul RHI's refusal, named.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome);

/// A lit pipeline's twin under the decals (D339).
[[nodiscard]] mrhiGraphicsPipelineDef decaledOf(mrhiGraphicsPipelineDef def, std::string_view label);

/// The prepass also leaving each point's surface (D327, D331), whole and
/// masked, from the prepass's pipeline.
[[nodiscard]] std::array<mrhiGraphicsPipelineDef, 2> surfacing(const mrhiGraphicsPipelineDef& prepass);

} // namespace rawframe::render_scene_gpu
