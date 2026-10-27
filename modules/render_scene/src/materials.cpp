#include "rawframe/render_scene/materials.h"

#include "rawframe/material/instance.h"
#include "rawframe/render_scene/errors.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace rawframe::render_scene {

SceneTexture sceneTextureOf(const material::SampledTexture& texture) noexcept {
    return {.id = texture.id, .filter = texture.filter, .address = texture.address};
}

namespace {

result::Result<SceneMaterial> readSurface(std::uint64_t identity,
                                          base::Bits128 id,
                                          const CookedMaterial& cooked,
                                          material::Quality quality,
                                          Programs& programs,
                                          const CookedMaterials& bases,
                                          bool instance) {
    const std::span<const std::byte> kBytes{cooked.bytes};
    if (cooked.representation == material::kInstanceRepresentation) {
        RAWFRAME_TRY_ASSIGN(const material::MaterialInstance kInstance, material::decodeInstance(kBytes));
        if (!instance) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               kRenderSceneDomain,
                                                               code(RenderSceneError::MaterialUnreadable),
                                                               "a material instance's base is another instance")
                                                      .error()};
        }
        RAWFRAME_TRY_ASSIGN(const CookedMaterial kBase, bases(kInstance.base));
        RAWFRAME_TRY_ASSIGN(SceneMaterial made,
                            readSurface(identity, kInstance.base, kBase, quality, programs, bases, false));
        std::ranges::copy(kInstance.parameters, made.blob.begin() + material::kBlobFloats);
        // The textures its texture inputs' overrides give, each at its
        // base's slot, its sampler state the base's.
        const std::array<SceneTexture*, 4> kSlots = {
            &made.textures.base, &made.textures.packed, &made.textures.emission, &made.textures.normal};
        for (std::size_t slot = 0; slot < kSlots.size(); ++slot) {
            if (kInstance.textures.at(slot) != 0) {
                kSlots.at(slot)->id = kInstance.textures.at(slot);
            }
        }
        return made;
    }
    if (cooked.representation == material::kProgramRepresentation) {
        ReadProgram& read = programs[id];
        if (read.program == nullptr || read.revision != cooked.revision) {
            RAWFRAME_TRY_ASSIGN(material::ProgramMaterial decoded, material::decodeProgram(kBytes));
            read = ReadProgram{.revision = cooked.revision,
                               .program = std::make_shared<const material::ProgramMaterial>(std::move(decoded))};
        }
        const std::shared_ptr<const material::ProgramMaterial>& program = read.program;
        const material::Material kStates{.shading = program->shading,
                                         .blend = program->blend,
                                         .alphaCutoff = program->alphaCutoff,
                                         .doubleSided = program->doubleSided};
        std::array<SceneTexture, 4> slots{};
        for (std::size_t at = 0; at < program->textures.size(); ++at) {
            slots.at(at) = sceneTextureOf(program->textures[at]);
        }
        return SceneMaterial{
            .id = identity,
            .blob = sceneBlobOf(material::blobOf(kStates), program->parameters),
            // A transmitting model is drawn after the opaque, over a copy of
            // what they made, as the translucent are.
            .translucent = program->blend == material::Blend::Translucent || program->transmits,
            .textures = {.base = slots[0], .packed = slots[1], .emission = slots[2], .normal = slots[3]},
            .program = program};
    }
    RAWFRAME_TRY_ASSIGN(const material::Qualities kQualities, material::decode(kBytes));
    const material::Material& kMaterial = kQualities.at(static_cast<std::size_t>(quality));
    return SceneMaterial{.id = identity,
                         .blob = sceneBlobOf(material::blobOf(kMaterial)),
                         .translucent = kMaterial.blend == material::Blend::Translucent,
                         .textures = {.base = sceneTextureOf(kMaterial.textures.base),
                                      .packed = sceneTextureOf(kMaterial.textures.packed),
                                      .emission = sceneTextureOf(kMaterial.textures.emission),
                                      .normal = sceneTextureOf(kMaterial.textures.normal)},
                         .program = nullptr};
}

} // namespace

result::Result<SceneMaterial> sceneMaterialOf(std::uint64_t identity,
                                              base::Bits128 id,
                                              const CookedMaterial& cooked,
                                              material::Quality quality,
                                              Programs& programs,
                                              const CookedMaterials& bases) {
    return readSurface(identity, id, cooked, quality, programs, bases, true);
}

} // namespace rawframe::render_scene
