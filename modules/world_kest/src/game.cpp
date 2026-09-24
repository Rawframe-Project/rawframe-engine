#include "rawframe/world_kest/game.h"

#include "rawframe/world_kest/errors.h"

#include <array>
#include <charconv>
#include <optional>

namespace rawframe::world_kest {

namespace {

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> found;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) {
            ++at;
        }
        const std::size_t kStart = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t') {
            ++at;
        }
        if (at > kStart) {
            found.push_back(line.substr(kStart, at - kStart));
        }
    }
    return found;
}

std::optional<world::Phase> phaseNamed(std::string_view name) noexcept {
    for (std::size_t index = 0; index < world::kPhaseCount; ++index) {
        const auto kPhase = static_cast<world::Phase>(index);
        if (world::describe(kPhase) == name) {
            return kPhase;
        }
    }
    return std::nullopt;
}

std::optional<world::Access> accessNamed(std::string_view name) noexcept {
    constexpr std::array<std::pair<std::string_view, world::Access>, 4> kNames = {{
        {"read", world::Access::Read},
        {"write", world::Access::Write},
        {"with", world::Access::With},
        {"without", world::Access::Without},
    }};
    for (const auto& [text, access] : kNames) {
        if (text == name) {
            return access;
        }
    }
    return std::nullopt;
}

std::unexpected<result::Error> badLine(std::size_t line, WorldKestError error, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(error), why)
            .error()
            .withContext("line", std::to_string(line))};
}

bool declared(const GameDescription& game, std::string_view component) {
    for (const GameComponent& known : game.components) {
        if (known.name == component) {
            return true;
        }
    }
    return false;
}

} // namespace

result::Result<GameDescription> parseGame(std::string_view text) {
    GameDescription game;
    bool haveProgram = false;
    std::size_t number = 0;
    // Names are checked once every component line has been read, so a
    // description may list components after the systems that use them.
    std::vector<std::pair<std::size_t, std::string>> uses;
    while (!text.empty()) {
        const std::size_t kEnd = text.find('\n');
        std::string_view line = text.substr(0, kEnd);
        text = kEnd == std::string_view::npos ? std::string_view{} : text.substr(kEnd + 1);
        if (++number > kMaximumGameLines) {
            return badLine(number, WorldKestError::BadGameLine, "a game description has too many lines");
        }
        if (const std::size_t kComment = line.find('#'); kComment != std::string_view::npos) {
            line = line.substr(0, kComment);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::vector<std::string_view> kWords = words(line);
        if (kWords.empty()) {
            continue;
        }
        const std::string_view kKeyword = kWords[0];
        if (kKeyword == "program") {
            if (haveProgram || kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a game names exactly one program");
            }
            game.program = kWords[1];
            haveProgram = true;
        } else if (kKeyword == "component") {
            const base::Bits128Parse kId =
                kWords.size() == 4 ? schema::parseStableIdText(kWords[1]) : base::Bits128Parse{};
            if (!kId.parsed || kId.value == base::Bits128{}) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a component line is `component <uuid> <name> <Kest type>`");
            }
            game.components.push_back(GameComponent{.id = schema::ComponentTypeId{kId.value},
                                                    .name = std::string{kWords[2]},
                                                    .kestType = std::string{kWords[3]}});
        } else if (kKeyword == "system") {
            const auto kPhase = kWords.size() >= 4 ? phaseNamed(kWords[2]) : std::nullopt;
            if (!kPhase) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a system line is `system <identity> <phase> <entry>` then pairs");
            }
            GameSystem system{.identity = std::string{kWords[1]},
                              .phase = *kPhase,
                              .entry = std::string{kWords[3]},
                              .columns = {},
                              .after = {},
                              .before = {}};
            for (std::size_t at = 4; at < kWords.size(); at += 2) {
                const std::string_view kWhat = kWords[at];
                if (kWhat == "entities") {
                    system.columns.push_back(
                        GameColumn{.access = world::Access::Read, .component = {}, .entities = true});
                    --at;
                    continue;
                }
                if (at + 1 == kWords.size()) {
                    return badLine(number, WorldKestError::BadGameLine, "a system column or edge names nothing");
                }
                const std::string kName{kWords[at + 1]};
                if (kWhat == "after") {
                    system.after.push_back(kName);
                } else if (kWhat == "before") {
                    system.before.push_back(kName);
                } else if (const auto kAccess = accessNamed(kWhat)) {
                    system.columns.push_back(GameColumn{.access = *kAccess, .component = kName});
                    uses.emplace_back(number, kName);
                } else {
                    return badLine(number,
                                   WorldKestError::BadGameLine,
                                   "a system column is entities, or read, write, with, or without a component; an "
                                   "edge is after or before a system");
                }
            }
            game.systems.push_back(std::move(system));
        } else if (kKeyword == "spawn") {
            std::uint32_t count = 0;
            const auto kCount = kWords.size() >= 2
                                    ? std::from_chars(kWords[1].data(), kWords[1].data() + kWords[1].size(), count)
                                    : std::from_chars_result{nullptr, std::errc::invalid_argument};
            if (kCount.ec != std::errc{} || kCount.ptr != kWords[1].data() + kWords[1].size() || count == 0 ||
                count > kMaximumSpawnCount || kWords.size() < 3) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a spawn line is `spawn <count> <component> [field=value]...`");
            }
            GameSpawn spawn{.count = count, .components = {}};
            for (std::size_t at = 2; at < kWords.size(); ++at) {
                const std::string_view kWord = kWords[at];
                const std::size_t kEquals = kWord.find('=');
                if (kEquals == std::string_view::npos) {
                    spawn.components.push_back(GameSpawnComponent{.component = std::string{kWord}, .fields = {}});
                    uses.emplace_back(number, std::string{kWord});
                    continue;
                }
                if (spawn.components.empty() || kEquals == 0 || kEquals + 1 == kWord.size()) {
                    return badLine(
                        number, WorldKestError::BadGameLine, "a field value follows its component as `field=value`");
                }
                spawn.components.back().fields.push_back(GameFieldValue{
                    .field = std::string{kWord.substr(0, kEquals)}, .value = std::string{kWord.substr(kEquals + 1)}});
            }
            game.spawns.push_back(std::move(spawn));
        } else {
            return badLine(
                number, WorldKestError::BadGameLine, "a line starts with program, component, system, or spawn");
        }
    }
    if (!haveProgram) {
        return badLine(number, WorldKestError::BadGameLine, "a game names exactly one program");
    }
    for (const auto& [line, name] : uses) {
        if (!declared(game, name)) {
            return badLine(line, WorldKestError::UnknownName, "a line names a component the game does not declare");
        }
    }
    return game;
}

} // namespace rawframe::world_kest
