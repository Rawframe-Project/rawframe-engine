// A material's own program's pipelines (D485, D487): the scene's
// containers linked with the material at cook time (D484), the one the
// build's driver reads made a shader, and the engine's own models'
// pipelines that run its code made again with it as a frame first wants
// each: the lit ones, the prepass's that cut or leave surfaces, and, from
// the shadow's container, the masked casters' (D488).

#include "pipelines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

/// Which of a program material's containers the build's driver reads, as
/// the engine's own are chosen (RAWFRAME_SHADER_SUFFIX, D416): Vulkan's
/// and WebGPU's, Metal's, or Direct3D 12's.
constexpr std::size_t kContainer = RAWFRAME_PROGRAM_CONTAINER;
static_assert(kContainer < material::kProgramContainers);

constexpr std::array<std::string_view, 19> kLabels = {"rawframe.scene.program.lit",
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
                                                      "rawframe.scene.program.depth.surfaces.masked.multisampled",
                                                      "rawframe.scene.program.shadows.masked"};

/// What the device said refusing a program's `part`: the error and what it
/// names, one line.
std::string refusalOf(const result::Error& error, std::string_view part) {
    std::string made = std::string{part} + ": " + std::string{error.description()};
    for (const auto& each : error.context()) {
        made += ", " + std::string{each.key} + " " + std::string{each.value};
    }
    return made;
}

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
            const auto kBytesOf = [](const std::vector<std::byte>& bytes) {
                return std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
            };
            for (const auto& [kBytes, kInto] : {std::pair{&kProgram->containers, &shading.shader},
                                                std::pair{&kProgram->shadows, &shading.shadowShader}}) {
                if (const result::Status kMade = makeShader(kBytesOf(kBytes->at(kContainer)), *kInto);
                    !shading.refused && !kMade.has_value()) {
                    shading.refused = true;
                    shading.refusal = refusalOf(kMade.error(), kBytes == &kProgram->containers ? "scene" : "shadow");
                }
            }
        }
        // The prepass's surfaces are wanted once a view wants them (D337).
        const bool kSurfaced = asked_.at(static_cast<std::size_t>(Effect::Surfaces));
        const std::array<mrhiGraphicsPipelineDef, 2> kSurfacing = surfacing(prepass_);
        for (std::size_t variant = 0; variant < shading.variants.size() && !shading.refused; ++variant) {
            // The lit twelve, the prepass's six, then the masked casters'
            // (`variantOf`).
            const bool kLit = variant < 12;
            const bool kCasting = variant == 18;
            const std::size_t kPrepass = kLit || kCasting ? 0 : (variant - 12) % 3;
            const bool kDecaled = kLit && (variant % 6) >= 3;
            const bool kMultisampled = kLit ? variant >= 6 : !kCasting && variant >= 15;
            if (shading.asked.at(variant) || (kDecaled && !decaled) || (kMultisampled && !sampled) ||
                (!kLit && !kCasting && kPrepass > 0 && !kSurfaced)) {
                continue;
            }
            mrhiGraphicsPipelineDef def = kLit            ? shading_.at(variant % 3)
                                          : kCasting      ? cutCasting_
                                          : kPrepass == 0 ? cut_
                                                          : kSurfacing.at(kPrepass - 1);
            def.shader = kCasting ? shading.shadowShader : shading.shader;
            def.label = kLabels.at(variant).data();
            def.labelLength = kLabels.at(variant).size();
            if (kDecaled) {
                def = decaledOf(def, kLabels.at(variant));
            }
            if (kMultisampled) {
                def.sampleCount = samples;
            }
            shading.asked.at(variant) = true;
            if (const result::Status kAsked = ask(def, shading.variants.at(variant)); !kAsked.has_value()) {
                shading.refused = true;
                shading.refusal = refusalOf(kAsked.error(), kLabels.at(variant));
            }
        }
        for (std::size_t variant = 0; variant < shading.variants.size() && !shading.refused; ++variant) {
            if (shading.asked.at(variant) && !shading.variants.at(variant).ready) {
                const auto kAnswered = answered({&shading.variants.at(variant)});
                if (!kAnswered.has_value()) {
                    shading.refused = true;
                    shading.refusal = refusalOf(kAnswered.error(), kLabels.at(variant));
                }
            }
        }
    }
}

void Pipelines::tellRefused(RendererStatistics& statistics) const {
    statistics.programsRefused = 0;
    for (const auto& [kProgram, kShading] : programs) {
        if (kShading.refused) {
            ++statistics.programsRefused;
            if (statistics.programRefusal.empty()) {
                statistics.programRefusal = kShading.refusal;
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
