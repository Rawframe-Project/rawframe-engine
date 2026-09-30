#include "rawframe/game_textures/game_textures.h"

#include <algorithm>

namespace rawframe::game_textures {

namespace {

constexpr content::ResourceTypeId kTextureType{texture::kTextureType};

/// The cooked bytes read as the texture they are: every level checked, as
/// hostile input is.
result::Result<assets::DecodedForm> decodeTexture(const content::VerifiedContent& content) {
    RAWFRAME_TRY_ASSIGN(texture::Texture decoded, texture::decode(content.bytes()));
    std::uint64_t bytes = 0;
    for (const texture::Level& level : decoded.levels) {
        bytes += level.bytes.size();
    }
    return assets::DecodedForm{.value = std::make_shared<const texture::Texture>(std::move(decoded)), .bytes = bytes};
}

struct Wanted {
    std::uint64_t id = 0;
    content::ResourceId resource;
    assets::RequesterId requester;
    bool reported = false;
};

} // namespace

std::vector<content::AdmittedRepresentation> textureRepresentations() {
    return {
        {.type = kTextureType, .representation = *content::RepresentationId::parse(texture::kTextureRepresentation)}};
}

struct GameTextures::State {
    std::unique_ptr<assets::AssetSet> set;
    std::vector<Wanted> wanted;
    bool read = false;
};

GameTextures::GameTextures(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

GameTextures::~GameTextures() = default;

result::Result<std::unique_ptr<GameTextures>>
GameTextures::create(content::ContentStore& store,
                     execution::Executor& cpu,
                     execution::OwnerId owner,
                     execution::CancellationScope& parent,
                     const execution::MonotonicSource& clock,
                     std::vector<world_kest::GameTextureResource> declared,
                     std::uint64_t budgetBytes) {
    auto state = std::make_unique<State>();
    RAWFRAME_TRY_ASSIGN(
        state->set,
        assets::AssetSet::create(store,
                                 cpu,
                                 owner,
                                 parent,
                                 clock,
                                 {.type = kTextureType, .decode = &decodeTexture, .budgetBytes = budgetBytes}));
    for (const world_kest::GameTextureResource& texture : declared) {
        const content::ResourceRef kReference{.id = content::ResourceId{texture.texture}, .type = kTextureType};
        RAWFRAME_TRY_ASSIGN(const assets::RequesterId kRequester, state->set->request(kReference));
        state->wanted.push_back(Wanted{.id = texture.id, .resource = kReference.id, .requester = kRequester});
    }
    return std::unique_ptr<GameTextures>{new GameTextures{std::move(state)}};
}

TextureChanges GameTextures::update(std::uint64_t tick) {
    State& state = *state_;
    state.set->update(tick);
    TextureChanges changes;
    for (Wanted& wanted : state.wanted) {
        if (!wanted.reported && state.set->readiness(wanted.requester) == assets::Readiness::Failed) {
            wanted.reported = true;
            changes.failed.emplace_back(wanted.id, state.set->failure(wanted.requester)->clone());
        }
    }
    // Taken every update, so they never pile up: the canvas asks for each
    // texture's handle as it draws, so it draws the new revision anyway.
    for (const assets::ReloadEvent& event : state.set->takeReloadEvents()) {
        for (const Wanted& wanted : state.wanted) {
            if (wanted.resource != event.id) {
                continue;
            }
            if (event.outcome == assets::ReloadOutcome::Published) {
                changes.reloaded.push_back(wanted.id);
            } else {
                changes.notReloaded.emplace_back(wanted.id, event.failure->clone());
            }
        }
    }
    if (!state.read && counts().ready == state.wanted.size()) {
        state.read = true;
        changes.read = true;
    }
    return changes;
}

std::shared_ptr<const texture::Texture> GameTextures::texture(std::uint64_t id, std::uint64_t tick) const {
    const auto kWanted = std::ranges::find(state_->wanted, id, &Wanted::id);
    if (kWanted == state_->wanted.end()) {
        return nullptr;
    }
    const std::optional<assets::AssetHandle> kHandle = state_->set->handle(kWanted->requester);
    if (!kHandle.has_value()) {
        return nullptr;
    }
    auto shared = assets::Assets<texture::Texture>{*state_->set}.share(*kHandle, tick);
    return shared.has_value() ? std::move(*shared) : nullptr;
}

TextureCounts GameTextures::counts() const noexcept {
    TextureCounts counts{.bytes = state_->set->statistics().residentBytes};
    for (const Wanted& wanted : state_->wanted) {
        switch (state_->set->readiness(wanted.requester)) {
        case assets::Readiness::Ready:
            ++counts.ready;
            break;
        case assets::Readiness::Failed:
            ++counts.failed;
            break;
        default:
            ++counts.pending;
            break;
        }
    }
    return counts;
}

} // namespace rawframe::game_textures
