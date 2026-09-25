#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/mod.h"

#include <algorithm>

namespace rawframe::world_kest {

namespace {

result::Error refused(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(WorldKestError::ModRefused), why)
        .error();
}

} // namespace

result::Status checkMods(const GameDescription& game,
                         std::string_view gameSubject,
                         std::span<const ComposedMod> mods,
                         std::span<const std::string> gameHolds) {
    const GameModApi& api = game.mods;
    for (const ComposedMod& mod : mods) {
        if (api.policy == ModPolicy::Closed) {
            return std::unexpected<result::Error>{refused("the game takes no mods").withContext("mod", mod.subject)};
        }
        if (api.policy == ModPolicy::Curated && std::ranges::find(api.approved, mod.subject) == api.approved.end()) {
            return std::unexpected<result::Error>{
                refused("a curated game takes only the mods it approves").withContext("mod", mod.subject)};
        }
        if (mod.description.target != gameSubject) {
            return std::unexpected<result::Error>{refused("a mod targets another game")
                                                      .withContext("mod", mod.subject)
                                                      .withContext("target", mod.description.target)};
        }
        if (!accepts(mod.description.modApi, api.version)) {
            return std::unexpected<result::Error>{refused("the game's Mod API version is outside a mod's range")
                                                      .withContext("mod", mod.subject)
                                                      .withContext("version", std::to_string(api.version))};
        }
        for (const ModContribution& contribution : mod.description.contributions) {
            if (std::ranges::find(api.points, contribution.point, &GameExtensionPoint::name) == api.points.end()) {
                return std::unexpected<result::Error>{refused("a mod contributes to a point the game does not declare")
                                                          .withContext("mod", mod.subject)
                                                          .withContext("point", contribution.point)};
            }
        }
    }
    // Occupancy, over every mod at once: never decided by order.
    for (const GameExtensionPoint& point : api.points) {
        std::string claimants;
        std::size_t count = 0;
        for (const ComposedMod& mod : mods) {
            for (const ModContribution& contribution : mod.description.contributions) {
                if (contribution.point == point.name) {
                    claimants += (count == 0 ? "" : " ") + mod.subject + ":" + contribution.scene;
                    ++count;
                }
            }
        }
        if (point.exclusive && count > 1) {
            return std::unexpected<result::Error>{refused("an exclusive point has more than one claimant")
                                                      .withContext("point", point.name)
                                                      .withContext("claimants", claimants)};
        }
        if (point.required && count == 0 && std::ranges::find(gameHolds, point.accepts) == gameHolds.end()) {
            return std::unexpected<result::Error>{
                refused("a required point is filled by no mod and not by the game").withContext("point", point.name)};
        }
    }
    return {};
}

} // namespace rawframe::world_kest
