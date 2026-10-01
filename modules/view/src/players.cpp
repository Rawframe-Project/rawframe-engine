#include "rawframe/view/players.h"

namespace rawframe::view {

namespace {

template <typename Told> void keep(std::vector<std::optional<Told>>& told, std::size_t player, const Told& view) {
    if (player >= told.size()) {
        told.resize(player + 1);
    }
    told[player] = view;
}

template <typename Told> void drop(std::vector<std::optional<Told>>& told, std::size_t player) noexcept {
    if (player < told.size()) {
        told[player].reset();
    }
}

template <typename Camera, typename Told>
std::optional<Placed<Camera>>
place(const std::vector<std::optional<Told>>& told, std::size_t player, ViewSize window) noexcept {
    if (player >= told.size() || !told[player].has_value()) {
        return std::nullopt;
    }
    const Region& kRegion = told[player]->region;
    return Placed<Camera>{.left = kRegion.left * window.width,
                          .top = kRegion.top * window.height,
                          .size = {.width = kRegion.width * window.width, .height = kRegion.height * window.height},
                          .camera = told[player]->camera};
}

} // namespace

void PlayerViews::tell(std::size_t player, const Region& region, const Perspective& camera) {
    keep(perspectives_, player, Told<Perspective>{.region = region, .camera = camera});
}

void PlayerViews::tell(std::size_t player, const Region& region, const Orthographic& camera) {
    keep(orthographics_, player, Told<Orthographic>{.region = region, .camera = camera});
}

void PlayerViews::forgetPerspective(std::size_t player) noexcept {
    drop(perspectives_, player);
}

void PlayerViews::forgetOrthographic(std::size_t player) noexcept {
    drop(orthographics_, player);
}

std::optional<Placed<Perspective>> PlayerViews::perspective(std::size_t player) const noexcept {
    return place<Perspective>(perspectives_, player, window_);
}

std::optional<Placed<Orthographic>> PlayerViews::orthographic(std::size_t player) const noexcept {
    return place<Orthographic>(orthographics_, player, window_);
}

} // namespace rawframe::view
