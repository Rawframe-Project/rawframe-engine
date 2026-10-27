#pragma once

#include "rawframe/material/material.h"
#include "rawframe/render/device.h"
#include "rawframe/render_scene_gpu/renderer.h"
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
#include <string>
#include <string_view>

namespace rawframe::render_scene_gpu {

/// The shadow maps' depth: the sun's cascades' and the punctual
/// lights' atlas.
constexpr mrhiFormat kShadowFormat = mrhi_formatDepth32Float;

constexpr mrhiFormat kSceneFormat = mrhi_formatRgba16Float;
/// Where each texel's point moved since the frame before.
constexpr mrhiFormat kMotionFormat = mrhi_formatRg16Float;
constexpr mrhiFormat kDepthFormat = mrhi_formatDepth32Float;
/// Each point's normal and roughness, the prepass's auxiliary target when
/// a screen-space effect asks for it.
constexpr mrhiFormat kSurfaceFormat = mrhi_formatRgba16Float;
/// What of the light from all around reaches each point.
constexpr mrhiFormat kAmbientFormat = mrhi_formatR8Unorm;
/// What each point reflects of the picture before, and how much.
constexpr mrhiFormat kReflectionFormat = mrhi_formatRgba16Float;
/// The decals' normals' atlas: linear, as normal textures are.
constexpr mrhiFormat kNormalsFormat = mrhi_formatRgba8Unorm;
/// The reflection probes' atlas, as the pictures it holds are.
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

/// The models' passes' pipelines multisampled: the prepass's, whole
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
/// draw: lit, the opaque, the masked, and the translucent;
/// in the prepass, the masked cut, and each point's surface, whole and
/// masked; the masked casters into the shadow maps;
/// and, for a material that moves its vertices, the opaque models'
/// depth and the solid casters, which run no other material's code.
enum class Shade : std::uint8_t {
    Lit,
    Masked,
    Glass,
    Cut,
    Surfaces,
    CutSurfaces,
    CutCasting,
    Depth,
    Casting
};

/// A material's own program's pipelines: the engine's own
/// models' but for the shader, by what they draw; lit, plain and under
/// decals; each single- and multisampled; each asked for when a frame first
/// wants it (`variantOf`); refused where its shader or a pipeline could not
/// be made, its models then drawn by the engine's own.
struct ProgramShading {
    std::shared_ptr<const material::ProgramMaterial> program;
    mrhiShaderId shader{};
    /// The shadow's shader, which its masked casters are cut by.
    mrhiShaderId shadowShader{};
    std::array<Asked, 22> variants{};
    std::array<bool, 22> asked{};
    bool refused = false;
    /// What refused it, as the device said.
    std::string refusal;
};

/// Where a program's pipeline for `shade` lies among its variants: the lit
/// twelve, then the prepass's six, which no decal changes, then the masked
/// casters', which no view multisamples; then a moving material's depth,
/// single and multisampled, and its solid casters'.
[[nodiscard]] constexpr std::size_t variantOf(Shade shade, bool decaled, bool multisampled) noexcept {
    const auto kShade = static_cast<std::size_t>(shade);
    if (shade == Shade::CutCasting) {
        return 18;
    }
    if (shade == Shade::Depth) {
        return multisampled ? 20 : 19;
    }
    if (shade == Shade::Casting) {
        return 21;
    }
    if (kShade >= 3) {
        return 12U + (kShade - 3U) + (multisampled ? 3U : 0U);
    }
    return kShade + (decaled ? 3U : 0U) + (multisampled ? 6U : 0U);
}

/// The optional effects whose pipelines are asked for only when a view
/// first wants them: the prepass's surfaces target, which the
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
    PostProcess,
    Skinning
};
inline constexpr std::size_t kEffects = 13;

/// The scene's shaders, samplers, and pipelines: what every
/// frame draws with asked of the device at once, each effect's when a view
/// first wants it, made as the device answers, and destroyed with
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
    mrhiShaderId skinShader{};
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
    /// and FXAA's taps.
    mrhiSamplerId filteredSampler{};
    /// A material's texture's, by its declared filter and address:
    /// at `samplerOf`.
    std::array<mrhiSamplerId, 4> materialSamplers{};
    /// The shadow maps' casters, the depth prepass, the lit models, the sky
    /// behind them, the metering's two steps, the temporal pass, the
    /// picture, and FXAA over it.
    Asked casting;
    /// The masked casters, cut where their opacity falls below their
    /// cutoff.
    Asked cutCasting;
    Asked depth;
    /// The masked models' depth, cut where their opacity falls below their
    /// cutoff.
    Asked cutout;
    /// The prepass leaving each point's surface too, for the ambient
    /// occlusion, whole and masked.
    Asked surfaces;
    Asked cutSurfaces;
    /// The ambient occlusion, and its blur.
    Asked occlude;
    Asked blurOcclusion;
    /// The screen-space reflections.
    Asked march;
    /// The motion blur's tiles, their neighbors, and its gathering.
    Asked motionTiles;
    Asked motionNeighbors;
    Asked motionGather;
    /// The depth of field's halving, its bokeh, and its blend.
    Asked focusPrefilter;
    Asked focusBokeh;
    Asked focusCombine;
    /// The contact shadows.
    Asked contactShade;
    /// A decal's texture drawn into the colors' atlas, its normal texture
    /// into the normals', and the lit, masked, and translucent
    /// models under the frame's decals.
    Asked decalFill;
    Asked decalNormalFill;
    Asked litDecaled;
    Asked maskedLitDecaled;
    Asked glassDecaled;
    /// A reflection probe's picture drawn into the atlas.
    Asked probeFill;
    Asked lit;
    /// The masked models lit only where their depth is the prepass's.
    Asked maskedLit;
    /// The translucent models, blended over what is behind them.
    Asked glass;
    Asked sky;
    Asked histogram;
    Asked adapt;
    /// Posed models' vertices skinned.
    Asked skin;
    Asked temporal;
    Asked tonemap;
    Asked fxaa;
    /// The post processes: in scene-linear light, in the picture's
    /// display-referred light, and the picture graded only, the light the
    /// post processes before the tonemapper take.
    Asked postLinear;
    Asked postDisplay;
    Asked grade;
    /// The bloom's first halving, its others, and its doublings, added to
    /// the level above.
    Asked bloomFirst;
    Asked bloomDown;
    Asked bloomUp;
    /// The multisampled models' passes, and the samples a pixel they
    /// take: set once, before they are first asked for; nought until then.
    Multisampled multisampled;
    std::uint32_t samples = 0;
    /// Every material's own program a frame has named, by its program,
    /// held so no other takes its place while its pipelines live.
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
    /// multisampled (`sampled`); those asked before polled. A program refused
    /// is refused for good, and is no frame's failure.
    void
    wantPrograms(std::span<const std::shared_ptr<const material::ProgramMaterial>> named, bool decaled, bool sampled);

    /// The pipeline `program` draws `shade` with, made; none before, and
    /// for a program refused.
    [[nodiscard]] const Asked*
    programPipeline(const material::ProgramMaterial* program, Shade shade, bool decaled, bool sampled) const;

    /// The programs refused into `statistics`: how many, and why the first
    /// was, if none was told before; and how many are held.
    void tellRefused(RendererStatistics& statistics) const;
    /// Lets go of each program no material holds any longer, its pipelines
    /// all answered: a material's program changed while the game plays
    /// leaves its old one, whose pipelines would otherwise live as
    /// long as the renderer. Maul RHI retires what a frame in flight
    /// still uses once the frame is done.
    void forgetPrograms();

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
    /// The casters' pipelines, which a material's program's are made from.
    mrhiGraphicsPipelineDef casting_{};
    mrhiGraphicsPipelineDef cutCasting_{};
    mrhiGraphicsPipelineDef sky_{};
};

/// Where a material's filter and address put its sampler among
/// `Pipelines::materialSamplers`.
[[nodiscard]] constexpr std::size_t samplerOf(material::Filter filter, material::Address address) noexcept {
    return (static_cast<std::size_t>(filter) * 2) + static_cast<std::size_t>(address);
}

/// Maul RHI's refusal, named.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome);

/// A lit pipeline's twin under the decals.
[[nodiscard]] mrhiGraphicsPipelineDef decaledOf(mrhiGraphicsPipelineDef def, std::string_view label);

/// The prepass also leaving each point's surface, whole and
/// masked, from the prepass's pipeline.
[[nodiscard]] std::array<mrhiGraphicsPipelineDef, 2> surfacing(const mrhiGraphicsPipelineDef& prepass);

} // namespace rawframe::render_scene_gpu
