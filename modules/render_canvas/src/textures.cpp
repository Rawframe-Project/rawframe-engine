#include "rawframe/render_canvas/textures.h"

#include <algorithm>

namespace rawframe::render_canvas {

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
    assets::RequesterId requester;
    bool reported = false;
};

} // namespace

std::vector<content::AdmittedRepresentation> textureRepresentations() {
    return {
        {.type = kTextureType, .representation = *content::RepresentationId::parse(texture::kTextureRepresentation)}};
}

struct CanvasTextures::State {
    std::unique_ptr<assets::AssetSet> set;
    std::vector<Wanted> wanted;
};

CanvasTextures::CanvasTextures(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

CanvasTextures::~CanvasTextures() = default;

result::Result<std::unique_ptr<CanvasTextures>>
CanvasTextures::create(content::ContentStore& store,
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
        state->wanted.push_back(Wanted{.id = texture.id, .requester = kRequester});
    }
    return std::unique_ptr<CanvasTextures>{new CanvasTextures{std::move(state)}};
}

std::vector<std::pair<std::uint64_t, result::Error>> CanvasTextures::update(std::uint64_t tick) {
    State& state = *state_;
    state.set->update(tick);
    std::vector<std::pair<std::uint64_t, result::Error>> failed;
    for (Wanted& wanted : state.wanted) {
        if (!wanted.reported && state.set->readiness(wanted.requester) == assets::Readiness::Failed) {
            wanted.reported = true;
            failed.emplace_back(wanted.id, state.set->failure(wanted.requester)->clone());
        }
    }
    return failed;
}

std::shared_ptr<const texture::Texture> CanvasTextures::texture(std::uint64_t id, std::uint64_t tick) const {
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

TextureCounts CanvasTextures::counts() const noexcept {
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

} // namespace rawframe::render_canvas
