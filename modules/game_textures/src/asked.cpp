#include "rawframe/game_textures/asked.h"

#include "rawframe/content/errors.h"

#include <algorithm>

namespace rawframe::game_textures {

AskedTextures::AskedTextures(std::optional<TextureReading> reading,
                             std::vector<world_kest::GameTextureResource> declared,
                             std::uint64_t budgetBytes)
    : reading_(reading), declared_(std::move(declared)), budgetBytes_(budgetBytes) {
}

std::optional<result::Error> AskedTextures::ask(std::uint64_t id) {
    if (id == 0 || asked_.contains(id)) {
        return std::nullopt;
    }
    std::unique_ptr<GameTextures>& reader = asked_[id];
    const auto kDeclared = std::ranges::find(declared_, id, &world_kest::GameTextureResource::id);
    if (kDeclared == declared_.end()) {
        return result::fail(result::ErrorClass::NotFound,
                            content::kContentDomain,
                            content::code(content::ContentError::ResourceNotFound),
                            "the game declares no such texture")
            .error();
    }
    if (!reading_.has_value()) {
        return std::nullopt;
    }
    ++read_;
    auto made = GameTextures::create(*reading_->store,
                                     *reading_->cpu,
                                     reading_->owner,
                                     *reading_->scope,
                                     *reading_->clock,
                                     {*kDeclared},
                                     budgetBytes_);
    if (!made.has_value()) {
        return std::move(made).error();
    }
    reader = std::move(*made);
    return std::nullopt;
}

std::vector<std::pair<std::uint64_t, result::Error>> AskedTextures::update(std::uint64_t tick) {
    std::vector<std::pair<std::uint64_t, result::Error>> failed;
    for (const auto& [kId, kReader] : asked_) {
        if (kReader == nullptr) {
            continue;
        }
        for (auto& each : kReader->update(tick).failed) {
            failed.push_back(std::move(each));
        }
    }
    return failed;
}

std::shared_ptr<const texture::Texture> AskedTextures::texture(std::uint64_t id, std::uint64_t tick) const {
    const auto kAsked = asked_.find(id);
    return kAsked != asked_.end() && kAsked->second != nullptr ? kAsked->second->texture(id, tick) : nullptr;
}

std::uint64_t AskedTextures::ready() const noexcept {
    std::uint64_t ready = 0;
    for (const auto& [kId, kReader] : asked_) {
        ready += kReader != nullptr ? kReader->counts().ready : 0;
    }
    return ready;
}

} // namespace rawframe::game_textures
