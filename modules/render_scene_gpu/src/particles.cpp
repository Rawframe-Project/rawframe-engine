#include "particles.h"

#include "tables.h"

#include <cstddef>
#include <cstdint>

namespace rawframe::render_scene_gpu {

particles_gpu::ViewBlock particleViewOf(const render_scene::SceneFrame& frame, const FrameBlock& block) noexcept {
    // The eye's right and up: the view's first two rows.
    return particles_gpu::ViewBlock{.viewProjection = block.viewProjection,
                                    .right = {frame.view[0], frame.view[4], frame.view[8], frame.particles.clock},
                                    .up = {frame.view[1], frame.view[5], frame.view[9], particles::kClockPeriod},
                                    .lens = {frame.projection[14], 0, 0, 0}};
}

std::vector<particles_gpu::Material> particleMaterialsOf(std::span<const render_scene::MaterialBlob> materials,
                                                         std::span<const render_scene::SceneTextures> textures,
                                                         const render::DeviceTextures& held,
                                                         const Pipelines& pipelines) {
    const auto kBound = [&held, &pipelines](const render_scene::SceneTexture& texture) {
        mrhiBinding image{};
        mrhiBinding filter{};
        bindTexture(held, pipelines, image, filter, texture);
        return particles_gpu::BoundTexture{
            .texture = render::requestKey(image.resource.index1, image.resource.generation),
            .sampler = render::requestKey(filter.sampler.index1, filter.sampler.generation)};
    };
    std::vector<particles_gpu::Material> made;
    made.reserve(materials.size());
    for (std::size_t place = 0; place < materials.size(); ++place) {
        const render_scene::MaterialBlob& kBlob = materials[place];
        const render_scene::SceneTextures kTextures =
            place < textures.size() ? textures[place] : render_scene::SceneTextures{};
        // The blob's flags (ADR-0031, D303): two where the base texture's
        // color multiplies the base color, four where its alpha multiplies
        // the opacity, eight where the emission texture's color multiplies
        // the emission.
        const auto kFlags = static_cast<std::uint32_t>(kBlob[15]);
        const bool kTinted = (kFlags & 2U) != 0;
        const bool kShaped = (kFlags & 4U) != 0;
        const bool kGlowing = (kFlags & 8U) != 0;
        const std::array<float, 4> kBase = {kBlob[0], kBlob[1], kBlob[2], 0};
        const std::array<float, 4> kEmission = {kBlob[8], kBlob[9], kBlob[10], 0};
        particles_gpu::MaterialBlock block{.color = kTinted ? std::array<float, 4>{} : kBase,
                                           .colorTexture = kTinted ? kBase : std::array<float, 4>{},
                                           .emission = kGlowing ? std::array<float, 4>{} : kEmission,
                                           .emissionTexture = kGlowing ? kEmission : std::array<float, 4>{},
                                           .baseMap = {kBlob[16], kBlob[17], kBlob[18], kBlob[19]},
                                           .emissionMap = {kBlob[24], kBlob[25], kBlob[26], kBlob[27]},
                                           .flags = {kShaped ? particles_gpu::kShapedByTexture : 0U, 0, 0, 0}};
        (kShaped ? block.colorTexture : block.color)[3] = kBlob[12];
        made.push_back(particles_gpu::Material{.block = block,
                                               .blend = particles_gpu::Blend::Over,
                                               .color = kBound(kTextures.base),
                                               .emission = kBound(kTextures.emission)});
    }
    // The lines' (D464), after the frame's own: white, whole, untextured,
    // so a line's color stands as it is, whatever the exposure.
    made.push_back(particles_gpu::Material{
        .block =
            particles_gpu::MaterialBlock{.color = {1, 1, 1, 1}, .baseMap = {1, 1, 0, 0}, .emissionMap = {1, 1, 0, 0}},
        .blend = particles_gpu::Blend::Over,
        .color = kBound(render_scene::SceneTexture{}),
        .emission = kBound(render_scene::SceneTexture{})});
    return made;
}

} // namespace rawframe::render_scene_gpu
