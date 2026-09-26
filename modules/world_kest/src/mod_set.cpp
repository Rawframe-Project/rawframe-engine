#include "rawframe/content/composition_record.h"
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

result::Result<std::vector<SetAsideClaim>> checkMods(const GameDescription& game,
                                                     std::string_view gameSubject,
                                                     std::span<const ComposedMod> mods,
                                                     std::span<const std::string> gameHolds) {
    const GameModApi& api = game.mods;
    if (mods.size() > content::kMaximumCompositionMods) {
        return std::unexpected<result::Error>{refused("more mods than a Composition names")};
    }
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
        for (const ModReplacement& replacement : mod.description.replacements) {
            if (!kKnown(replacement.point, GameExtensionPoint::Kind::Replacement)) {
                return std::unexpected<result::Error>{
                    refused("a mod replaces through a point the game does not declare as a replacement")
                        .withContext("mod", mod.subject)
                        .withContext("point", replacement.point)};
            }
        }
        for (const ModProvider& provider : mod.description.providers) {
            if (!kKnown(provider.point, GameExtensionPoint::Kind::Service)) {
                return std::unexpected<result::Error>{
                    refused("a mod provides a service point the game does not declare")
                        .withContext("mod", mod.subject)
                        .withContext("point", provider.point)};
            }
        }
    }
    // Occupancy, over every mod at once: never decided by order.
    std::vector<SetAsideClaim> setAside;
    for (const GameExtensionPoint& point : api.points) {
        std::string claimants;
        std::size_t count = 0;
        // How many claims each mod makes on the point.
        std::vector<std::size_t> claims(mods.size(), 0);
        for (std::size_t at = 0; at < mods.size(); ++at) {
            const ComposedMod& mod = mods[at];
            const auto kClaim = [&](std::string_view what) {
                // Named only where an exclusive point's refusal needs them.
                if (point.exclusive) {
                    claimants += (count == 0 ? "" : " ") + mod.subject + ":" + std::string{what};
                }
                ++count;
                ++claims[at];
            };
            for (const ModContribution& contribution : mod.description.contributions) {
                if (contribution.point == point.name) {
                    kClaim(contribution.scene);
                }
            }
            for (const ModReplacement& replacement : mod.description.replacements) {
                if (replacement.point == point.name) {
                    kClaim(replacement.function);
                }
            }
            for (const ModProvider& provider : mod.description.providers) {
                if (provider.point == point.name) {
                    kClaim(provider.function);
                }
            }
            for (const ModHandler& handler : mod.description.handlers) {
                if (handler.point == point.name) {
                    kClaim(handler.function);
                }
            }
        }
        // The game's own order settles an exclusive point: the first mod it
        // prefers that makes one claim keeps it, and every other mod's claims
        // on it are set aside (D202).
        if (point.exclusive && count > 1) {
            const auto kKeeps = [&](const std::string& subject) {
                const auto kMod = std::ranges::find(mods, subject, &ComposedMod::subject);
                return kMod != mods.end() && claims[static_cast<std::size_t>(kMod - mods.begin())] > 0;
            };
            const auto kKeeper = std::ranges::find_if(point.preferred, kKeeps);
            const auto kMod = kKeeper == point.preferred.end()
                                  ? mods.end()
                                  : std::ranges::find(mods, *kKeeper, &ComposedMod::subject);
            if (kMod != mods.end() && claims[static_cast<std::size_t>(kMod - mods.begin())] == 1) {
                for (std::size_t at = 0; at < mods.size(); ++at) {
                    if (claims[at] > 0 && at != static_cast<std::size_t>(kMod - mods.begin())) {
                        setAside.push_back(SetAsideClaim{.mod = mods[at].subject, .point = point.name});
                    }
                }
                count = 1;
            }
        }
        const std::size_t kLimit =
            point.kind == GameExtensionPoint::Kind::Data ? kMaximumPointContributions : kMaximumEventHandlers;
        if (count > kLimit) {
            return std::unexpected<result::Error>{refused("a point has more claimants than its limit")
                                                      .withContext("point", point.name)
                                                      .withContext("limit", std::to_string(kLimit))};
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
    return setAside;
}

} // namespace rawframe::world_kest
