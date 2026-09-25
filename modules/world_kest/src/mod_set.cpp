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
        // Values go to data points, handlers to event points.
        const auto kKnown = [&api](std::string_view name, GameExtensionPoint::Kind kind) {
            return std::ranges::any_of(api.points, [name, kind](const GameExtensionPoint& point) {
                return point.name == name && point.kind == kind;
            });
        };
        for (const ModContribution& contribution : mod.description.contributions) {
            if (!kKnown(contribution.point, GameExtensionPoint::Kind::Data)) {
                return std::unexpected<result::Error>{
                    refused("a mod contributes values to a data point the game does not declare")
                        .withContext("mod", mod.subject)
                        .withContext("point", contribution.point)};
            }
        }
        for (const ModHandler& handler : mod.description.handlers) {
            if (!kKnown(handler.point, GameExtensionPoint::Kind::Event)) {
                return std::unexpected<result::Error>{refused("a mod handles an event point the game does not declare")
                                                          .withContext("mod", mod.subject)
                                                          .withContext("point", handler.point)};
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
            for (const ModHandler& handler : mod.description.handlers) {
                if (handler.point == point.name) {
                    claimants += (count == 0 ? "" : " ") + mod.subject + ":" + handler.function;
                    ++count;
                }
            }
        }
        if (point.exclusive && count > 1) {
            return std::unexpected<result::Error>{refused("an exclusive point has more than one claimant")
                                                      .withContext("point", point.name)
                                                      .withContext("claimants", claimants)};
        }
        // The game fills a data point itself where its scenes hold the
        // component; an event point only a mod fills.
        const bool kGameFills = point.kind == GameExtensionPoint::Kind::Data &&
                                std::ranges::find(gameHolds, point.accepts) != gameHolds.end();
        if (point.required && count == 0 && !kGameFills) {
            return std::unexpected<result::Error>{
                refused("a required point is filled by no mod and not by the game").withContext("point", point.name)};
        }
    }
    return {};
}

} // namespace rawframe::world_kest
