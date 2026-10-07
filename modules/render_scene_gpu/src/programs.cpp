// A material's own program's lit pipelines (D485): the scene's containers
// linked with the material at cook time (D484), the one the build's driver
// reads made a shader, and the engine's own lit models' pipelines made
// again with it as a frame first wants each.

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

constexpr std::array<std::string_view, 12> kLabels = {"rawframe.scene.program.lit",
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
                                                      "rawframe.scene.program.glass.decaled.multisampled"};

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
        for (std::size_t variant = 0; variant < shading.variants.size() && !shading.refused; ++variant) {
            const bool kDecaled = (variant % 6) >= 3;
            const bool kMultisampled = variant >= 6;
            if (shading.asked.at(variant) || (kDecaled && !decaled) || (kMultisampled && !sampled)) {
                continue;
            }
            mrhiGraphicsPipelineDef def = shading_.at(variant % 3);
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
