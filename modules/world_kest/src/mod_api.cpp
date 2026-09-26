#include "mod_api.h"

#include "rawframe/content/product.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <charconv>

namespace rawframe::world_kest {

namespace {

std::unexpected<result::Error> badLine(std::size_t line, WorldKestError error, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(error), why)
            .error()
            .withContext("line", std::to_string(line))};
}

/// SPEC-0042's grammar for a namespace and a point's name:
/// `[a-z][a-z0-9_]{0,63}`.
bool validName(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && name.front() >= 'a' && name.front() <= 'z' &&
           std::ranges::all_of(name, [](char each) {
               return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
           });
}

} // namespace

bool modKeyword(std::string_view keyword) noexcept {
    return keyword == "mods" || keyword == "modapi" || keyword == "approve" || keyword == "extension";
}

result::Status
readModLine(std::span<const std::string_view> words, std::size_t line, GameDescription& game, ModLines& lines) {
    GameModApi& mods = game.mods;
    const std::string_view kKeyword = words[0];
    for (const std::string_view kWord : words) {
        lines.surfaceBytes += kWord.size() + 1;
    }
    if (lines.surfaceBytes > kMaximumModApiSurfaceBytes) {
        return badLine(line, WorldKestError::BadGameLine, "a game's Mod API surface is past its size limit");
    }
    if (kKeyword == "mods") {
        if (lines.policy != 0 || words.size() != 2 ||
            (words[1] != "closed" && words[1] != "curated" && words[1] != "open")) {
            return badLine(line, WorldKestError::BadGameLine, "a game has one mod policy, `mods closed|curated|open`");
        }
        mods.policy =
            words[1] == "open" ? ModPolicy::Open : (words[1] == "curated" ? ModPolicy::Curated : ModPolicy::Closed);
        lines.policy = line;
        return {};
    }
    if (kKeyword == "modapi") {
        std::uint32_t version = 0;
        const std::string_view kVersion = words.size() == 3 ? words[2] : std::string_view{};
        const auto [end, error] = std::from_chars(kVersion.data(), kVersion.data() + kVersion.size(), version);
        if (lines.modApi != 0 || words.size() != 3 || !validName(words[1]) || error != std::errc{} ||
            end != kVersion.data() + kVersion.size() || version == 0 || version > 0x7FFF'FFFFU) {
            return badLine(line,
                           WorldKestError::BadGameLine,
                           "a game has one Mod API, `modapi <namespace> <version>`, its version from 1");
        }
        mods.modNamespace = words[1];
        mods.version = version;
        lines.modApi = line;
        return {};
    }
    if (kKeyword == "approve") {
        if (words.size() != 2 || !content::validSubject(words[1]) || std::ranges::contains(mods.approved, words[1])) {
            return badLine(
                line, WorldKestError::BadGameLine, "a curated game approves each mod once, `approve <publisher/name>`");
        }
        mods.approved.emplace_back(words[1]);
        lines.firstApproval = lines.firstApproval == 0 ? line : lines.firstApproval;
        return {};
    }
    // `extension <name> data <component> multi|exclusive [required]`,
    // `extension <name> event <component> after <system> [write
    // <component>]... multi|exclusive [required]`, `extension <name> service
    // <component> exclusive [required]`, or `extension <name> replacement
    // <system> exclusive [required]`.
    if (words.size() >= 5 && words[2] == "replacement" && words[4] == "multi") {
        return badLine(
            line, WorldKestError::BadGameLine, "a replacement point is exclusive: a system has one function");
    }
    if (words.size() >= 5 && words[2] == "service" && words[4] == "multi") {
        return badLine(line,
                       WorldKestError::BadGameLine,
                       "a service point is exclusive: a call has one answer, and more than one provider would "
                       "leave which answers to order");
    }
    const bool kRequired = words.size() >= 5 && words.back() == "required";
    const std::span<const std::string_view> kBody = words.first(words.size() - (kRequired ? 1 : 0));
    const std::string_view kOccupancy = kBody.size() >= 5 ? kBody.back() : std::string_view{};
    GameExtensionPoint point{.name = words.size() >= 2 ? std::string{words[1]} : std::string{},
                             .kind = GameExtensionPoint::Kind::Data,
                             .accepts = kBody.size() >= 4 ? std::string{kBody[3]} : std::string{},
                             .after = {},
                             .writes = {},
                             .exclusive = kOccupancy == "exclusive",
                             .required = kRequired};
    bool shaped = kBody.size() >= 5 && validName(point.name) && (kOccupancy == "multi" || kOccupancy == "exclusive") &&
                  !std::ranges::contains(mods.points, point.name, &GameExtensionPoint::name);
    if (shaped && kBody[2] == "event") {
        point.kind = GameExtensionPoint::Kind::Event;
        // After the component: `after <system>`, then `write <component>`
        // pairs, then the occupancy.
        const std::span<const std::string_view> kClauses = kBody.subspan(4, kBody.size() - 5);
        shaped = kClauses.size() >= 2 && kClauses.size() % 2 == 0 && kClauses[0] == "after";
        for (std::size_t at = 0; shaped && at < kClauses.size(); at += 2) {
            if (at == 0) {
                point.after = kClauses[1];
                continue;
            }
            shaped = kClauses[at] == "write" && kClauses[at + 1] != point.accepts &&
                     !std::ranges::contains(point.writes, kClauses[at + 1]);
            point.writes.emplace_back(kClauses[at + 1]);
        }
    } else if (shaped && (kBody[2] == "service" || kBody[2] == "replacement")) {
        point.kind = kBody[2] == "service" ? GameExtensionPoint::Kind::Service : GameExtensionPoint::Kind::Replacement;
        // One replacement point per system: a system has one function.
        shaped = kBody.size() == 5 && point.exclusive &&
                 (point.kind == GameExtensionPoint::Kind::Service ||
                  std::ranges::none_of(mods.points, [&point](const GameExtensionPoint& other) {
                      return other.kind == GameExtensionPoint::Kind::Replacement && other.accepts == point.accepts;
                  }));
    } else {
        shaped = shaped && kBody.size() == 5 && kBody[2] == "data";
    }
    if (!shaped) {
        return badLine(line,
                       WorldKestError::BadGameLine,
                       "a game declares each point once, `extension <name> data <component> multi|exclusive "
                       "[required]`, `extension <name> event <component> after <system> [write <component>]... "
                       "multi|exclusive [required]`, `extension <name> service <component> exclusive [required]`, or "
                       "`extension <name> replacement <system> exclusive [required]`");
    }
    if (mods.points.size() == kMaximumExtensionPoints) {
        return badLine(line, WorldKestError::BadGameLine, "a game declares more extension points than the limit");
    }
    mods.points.push_back(std::move(point));
    lines.firstPoint = lines.firstPoint == 0 ? line : lines.firstPoint;
    return {};
}

result::Status checkModApi(const GameDescription& game, const ModLines& lines) {
    const GameModApi& mods = game.mods;
    if ((mods.policy != ModPolicy::Closed || !mods.points.empty()) && lines.modApi == 0) {
        return badLine(std::max(lines.policy, lines.firstPoint),
                       WorldKestError::BadGameLine,
                       "a game that mods may reach declares its Mod API, `modapi <namespace> <version>`");
    }
    if (lines.firstApproval != 0 && mods.policy != ModPolicy::Curated) {
        return badLine(lines.firstApproval, WorldKestError::BadGameLine, "only a curated game approves mods");
    }
    for (const GameExtensionPoint& point : mods.points) {
        if (point.kind == GameExtensionPoint::Kind::Replacement) {
            // A client predicts with the game's own function and runs no
            // mod, so a predicted system is the game's alone.
            const auto kSystem = std::ranges::find(game.systems, point.accepts, &GameSystem::identity);
            if (kSystem == game.systems.end() || kSystem->predicted) {
                return badLine(lines.firstPoint,
                               WorldKestError::UnknownName,
                               "a replacement point names a system the game declares, and not a predicted one");
            }
            continue;
        }
        const bool kKnown = std::ranges::contains(game.components, point.accepts, &GameComponent::name) &&
                            std::ranges::all_of(point.writes, [&game](const std::string& written) {
                                return std::ranges::contains(game.components, written, &GameComponent::name);
                            });
        if (!kKnown) {
            return badLine(lines.firstPoint,
                           WorldKestError::UnknownName,
                           "an extension point names only components the game declares");
        }
        if (point.kind == GameExtensionPoint::Kind::Event &&
            !std::ranges::contains(game.systems, point.after, &GameSystem::identity)) {
            return badLine(lines.firstPoint,
                           WorldKestError::UnknownName,
                           "an event point's handlers run after a system the game declares");
        }
    }
    return {};
}

} // namespace rawframe::world_kest
