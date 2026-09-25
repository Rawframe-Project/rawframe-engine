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
    // `extension <name> data <component> multi|exclusive [required]`.
    if (words.size() >= 3 && (words[2] == "event" || words[2] == "service" || words[2] == "replacement")) {
        return badLine(line,
                       WorldKestError::BadGameLine,
                       "event, service, and replacement points wait for mods' own machines; a point is `data` for now");
    }
    const bool kRequired = words.size() == 6 && words[5] == "required";
    if ((words.size() != 5 && !kRequired) || words[2] != "data" || !validName(words[1]) ||
        (words[4] != "multi" && words[4] != "exclusive") ||
        std::ranges::contains(mods.points, words[1], &GameExtensionPoint::name)) {
        return badLine(
            line,
            WorldKestError::BadGameLine,
            "a game declares each point once, `extension <name> data <component> multi|exclusive [required]`");
    }
    mods.points.push_back(GameExtensionPoint{.name = std::string{words[1]},
                                             .accepts = std::string{words[3]},
                                             .exclusive = words[4] == "exclusive",
                                             .required = kRequired});
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
        if (!std::ranges::contains(game.components, point.accepts, &GameComponent::name)) {
            return badLine(lines.firstPoint,
                           WorldKestError::UnknownName,
                           "an extension point accepts a component the game does not declare");
        }
    }
    return {};
}

} // namespace rawframe::world_kest
