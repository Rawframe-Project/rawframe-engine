// A material's own program's pipelines (D485, D487): the scene's
// containers linked with the material at cook time (D484), the one the
// build's driver reads made a shader, and the engine's own models'
// pipelines that run its code made again with it as a frame first wants
// each: the lit ones, and the prepass's that cut or leave surfaces.

#include "pipelines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::render_scene_gpu {

namespace {

/// Which of a program material's containers the build's driver reads, as
/// the engine's own are chosen (RAWFRAME_SHADER_SUFFIX, D416): Vulkan's
/// and WebGPU's, Metal's, or Direct3D 12's.
constexpr std::size_t kContainer = RAWFRAME_PROGRAM_CONTAINER;
static_assert(kContainer < material::kProgramContainers);

constexpr std::array<std::string_view, 18> kLabels = {"rawframe.scene.program.lit",
                                                      "rawframe.scene.program.masked",
                                                      "rawframe.scene.program.glass",
                                                      "rawframe.scene.program.lit.decaled",
                                                      "rawframe.scene.program.masked.decaled",
                                                      "rawframe.scene.program.glass.decaled",
                                                      "rawframe.scene.program.lit.multisampled",
                                                      "rawframe.scene.program.masked.multisampled",
                                                      "rawframe.scene.program.glass.multisampled",
                                                      "rawframe.scene.program.lit.decaled.multisampled",
                                                      "rawframe.scene.program.masked.decaled.multisampled",
                                                      "rawframe.scene.program.glass.decaled.multisampled",
                                                      "rawframe.scene.program.depth.masked",
                                                      "rawframe.scene.program.depth.surfaces",
                                                      "rawframe.scene.program.depth.surfaces.masked",
                                                      "rawframe.scene.program.depth.masked.multisampled",
                                                      "rawframe.scene.program.depth.surfaces.multisampled",
                                                      "rawframe.scene.program.depth.surfaces.masked.multisampled"};

} // namespace

void Pipelines::wantPrograms(std::span<const std::shared_ptr<const material::ProgramMaterial>> named,
                             bool decaled,
                             bool sampled) {
    for (const std::shared_ptr<const material::ProgramMaterial>& kProgram : named) {
        if (kProgram == nullptr) {
            continue;
        }
        auto [at, made] = programs.try_emplace(kProgram.get());
        ProgramShading& shading = at->second;
        if (made) {
            shading.program = kProgram;
            const std::vector<std::byte>& kBytes = kProgram->containers.at(kContainer);
            const std::span<const std::uint8_t> kContainerBytes{reinterpret_cast<const std::uint8_t*>(kBytes.data()),
                                                                kBytes.size()};
            shading.refused = !makeShader(kContainerBytes, shading.shader).has_value();
        }
        // The prepass's surfaces are wanted once a view wants them (D337).
        const bool kSurfaced = asked_.at(static_cast<std::size_t>(Effect::Surfaces));
        const std::array<mrhiGraphicsPipelineDef, 2> kSurfacing = surfacing(prepass_);
        for (std::size_t variant = 0; variant < shading.variants.size() && !shading.refused; ++variant) {
            // The lit twelve, then the prepass's six (`variantOf`).
            const bool kLit = variant < 12;
            const std::size_t kPrepass = kLit ? 0 : (variant - 12) % 3;
            const bool kDecaled = kLit && (variant % 6) >= 3;
            const bool kMultisampled = kLit ? variant >= 6 : variant >= 15;
            if (shading.asked.at(variant) || (kDecaled && !decaled) || (kMultisampled && !sampled) ||
                (!kLit && kPrepass > 0 && !kSurfaced)) {
                continue;
            }
            mrhiGraphicsPipelineDef def = kLit            ? shading_.at(variant % 3)
                                          : kPrepass == 0 ? cut_
                                                          : kSurfacing.at(kPrepass - 1);
            def.shader = shading.shader;
            def.label = kLabels.at(variant).data();
            def.labelLength = kLabels.at(variant).size();
            if (kDecaled) {
                def = decaledOf(def, kLabels.at(variant));
            }
            if (kMultisampled) {
                def.sampleCount = samples;
            }
            shading.asked.at(variant) = true;
            shading.refused = !ask(def, shading.variants.at(variant)).has_value();
        }
        for (std::size_t variant = 0; variant < shading.variants.size() && !shading.refused; ++variant) {
            if (shading.asked.at(variant) && !shading.variants.at(variant).ready) {
                const auto kAnswered = answered({&shading.variants.at(variant)});
                shading.refused = !kAnswered.has_value();
            }
        }
    }
}

const Asked*
Pipelines::programPipeline(const material::ProgramMaterial* program, Shade shade, bool decaled, bool sampled) const {
    const auto kAt = programs.find(program);
    if (kAt == programs.end() || kAt->second.refused) {
        return nullptr;
    }
    const Asked& kVariant = kAt->second.variants.at(variantOf(shade, decaled, sampled));
    return kVariant.ready ? &kVariant : nullptr;
}

} // namespace rawframe::render_scene_gpu
