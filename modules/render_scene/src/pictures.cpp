#include "pictures.h"

#include "rawframe/composition/composition.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/graph/graph.h"

#include <algorithm>
#include <set>

namespace rawframe::render_scene {

namespace {

constexpr diagnostics::EventIdentity kTextureUnknown{"scene", "material_texture_unknown"};
constexpr diagnostics::EventIdentity kTexturesUnavailable{"scene", "textures_unavailable"};
constexpr diagnostics::EventIdentity kTextureUnread{"scene", "texture_unavailable"};
constexpr diagnostics::EventIdentity kTextureReloaded{"scene", "texture_reloaded"};
constexpr diagnostics::EventIdentity kTexturesRead{"scene", "textures_read"};
constexpr diagnostics::EventIdentity kPictureUnknown{"scene", "picture_unknown"};
constexpr diagnostics::EventIdentity kPictureUnread{"scene", "picture_unavailable"};
/// The decoded levels the materials' textures may hold.
constexpr std::uint64_t kTextureBudgetBytes = std::uint64_t{256} * 1024 * 1024;

} // namespace

result::Status ScenePictures::read(composition::ParticipantContext& context,
                                   const world_kest::GameFiles& files,
                                   std::vector<SceneMaterial>& materials,
                                   std::vector<ScenePostProcessMaterial>& postProcesses) {
    std::set<std::uint64_t> sampled;
    std::vector<std::pair<std::uint64_t, SceneTexture*>> named;
    for (SceneMaterial& each : materials) {
        for (SceneTexture* texture :
             {&each.textures.base, &each.textures.packed, &each.textures.emission, &each.textures.normal}) {
            named.emplace_back(each.id, texture);
        }
    }
    for (ScenePostProcessMaterial& each : postProcesses) {
        named.emplace_back(each.id, &each.texture);
    }
    for (const auto& [kMaterial, texture] : named) {
        if (texture->id == 0) {
            continue;
        }
        // A render texture is drawn, not read (D361).
        if (std::ranges::contains(
                files.description().renderTextures, texture->id, &world_kest::GameRenderTexture::id)) {
            continue;
        }
        if (std::ranges::find(files.textures(), texture->id, &world_kest::GameTextureResource::id) ==
            files.textures().end()) {
            const auto kPath = std::ranges::find(files.materials(), kMaterial, &world_kest::GameMaterialResource::id);
            unknownTextures_.emplace_back(kPath != files.materials().end() ? kPath->path : std::string{},
                                          graph::nodeIdText(texture->id));
            texture->id = 0;
            continue;
        }
        sampled.insert(texture->id);
    }
    sampled_ = sampled.size();
    if (!context.has(game_content::kGameContent.name) || context.cpuExecutor() == nullptr) {
        return {};
    }
    RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
    if (!content->held()) {
        return {};
    }
    RAWFRAME_TRY(content->admit(game_textures::textureRepresentations()));
    // What reading the sky's picture needs, when the World names one.
    pictures_ = game_textures::AskedTextures{game_textures::TextureReading{.store = &content->store(),
                                                                           .cpu = context.cpuExecutor(),
                                                                           .owner = context.owner(),
                                                                           .scope = &context.scope(),
                                                                           .clock = &context.clock()},
                                             files.textures(),
                                             kTextureBudgetBytes};
    if (sampled.empty()) {
        return {};
    }
    std::vector<world_kest::GameTextureResource> declared;
    for (const world_kest::GameTextureResource& each : files.textures()) {
        if (sampled.contains(each.id)) {
            declared.push_back(each);
        }
    }
    auto textures = game_textures::GameTextures::create(content->store(),
                                                        *context.cpuExecutor(),
                                                        context.owner(),
                                                        context.scope(),
                                                        context.clock(),
                                                        std::move(declared),
                                                        kTextureBudgetBytes);
    if (textures.has_value()) {
        textures_ = std::move(*textures);
    } else {
        unreadTextures_ = std::string{textures.error().description()};
    }
    return {};
}

void ScenePictures::start(const diagnostics::Emitter& emitter) {
    emitter_ = emitter;
    for (const auto& [kPath, kTexture] : unknownTextures_) {
        emitter_.log(diagnostics::Severity::Warning,
                     kTextureUnknown,
                     "a material samples a texture the game does not declare: it is sampled as white",
                     {diagnostics::field("material", kPath), diagnostics::field("texture", kTexture)});
    }
    if (unreadTextures_.has_value()) {
        emitter_.log(diagnostics::Severity::Warning,
                     kTexturesUnavailable,
                     "the materials' textures could not be asked for: they are sampled as white",
                     {diagnostics::field("reason", *unreadTextures_)});
    }
}

void ScenePictures::ask(std::uint64_t id) noexcept {
    const std::optional<result::Error> kNone = pictures_.ask(id);
    if (!kNone.has_value()) {
        return;
    }
    if (kNone->errorClass() == result::ErrorClass::NotFound) {
        emitter_.log(diagnostics::Severity::Warning,
                     kPictureUnknown,
                     "the World names a picture the game does not declare: it has none",
                     {diagnostics::field("texture", graph::nodeIdText(id))});
        return;
    }
    emitter_.log(diagnostics::Severity::Warning,
                 kPictureUnread,
                 "a picture the World names could not be asked for: it has none",
                 {diagnostics::field("texture", graph::nodeIdText(id)),
                  diagnostics::field("reason", std::string{kNone->description()})});
}

void ScenePictures::askAll(const SceneFrame& frame) noexcept {
    ask(frame.lights.environment);
    ask(frame.grading.table);
    for (const SceneProbe& kProbe : frame.probes) {
        ask(kProbe.environment);
    }
    for (const SceneDecal& kDecal : frame.decals) {
        ask(kDecal.texture);
        ask(kDecal.normal);
    }
}

void ScenePictures::update(std::uint64_t tick) noexcept {
    for (const auto& [kId, kError] : pictures_.update(tick)) {
        emitter_.log(diagnostics::Severity::Warning,
                     kPictureUnread,
                     "a picture the World names could not be read: it has none",
                     {diagnostics::field("texture", graph::nodeIdText(kId)),
                      diagnostics::field("reason", std::string{kError.description()})});
    }
    if (textures_ == nullptr) {
        return;
    }
    const game_textures::TextureChanges kChanges = textures_->update(tick);
    for (const auto& [kId, kError] : kChanges.failed) {
        emitter_.log(diagnostics::Severity::Warning,
                     kTextureUnread,
                     "a material's texture could not be read: it is sampled as white",
                     {diagnostics::field("texture", graph::nodeIdText(kId)),
                      diagnostics::field("reason", std::string{kError.description()})});
    }
    for (const std::uint64_t kId : kChanges.reloaded) {
        emitter_.log(diagnostics::Severity::Info,
                     kTextureReloaded,
                     "a material's texture was replaced by its new revision",
                     {diagnostics::field("texture", graph::nodeIdText(kId))});
    }
    if (kChanges.read) {
        emitter_.log(diagnostics::Severity::Info,
                     kTexturesRead,
                     "the materials' textures were read",
                     {diagnostics::field("textures", static_cast<std::uint64_t>(textures_->counts().ready))});
    }
}

std::shared_ptr<const texture::Texture> ScenePictures::texture(std::uint64_t id, std::uint64_t tick) const {
    if (auto picture = pictures_.texture(id, tick)) {
        return picture;
    }
    return textures_ != nullptr ? textures_->texture(id, tick) : nullptr;
}

std::uint64_t ScenePictures::texturesReady() const noexcept {
    return textures_ != nullptr ? static_cast<std::uint64_t>(textures_->counts().ready) : 0;
}

} // namespace rawframe::render_scene
