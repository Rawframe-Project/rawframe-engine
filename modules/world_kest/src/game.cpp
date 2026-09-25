#include "rawframe/world_kest/game.h"

#include "physics_facts.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_replication/perception.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>

namespace rawframe::world_kest {

namespace {

/// Sixteen hexadecimal digits, as identities are written in lines.
std::optional<std::uint64_t> parseHex64(std::string_view word) noexcept {
    std::uint64_t value = 0;
    const auto kRead = std::from_chars(word.data(), word.data() + word.size(), value, 16);
    if (word.size() != 16 || kRead.ec != std::errc{} || kRead.ptr != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

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
    std::size_t actionsLine = 0;
    std::size_t admissionLine = 0;
    std::size_t mixerLine = 0;
    std::size_t firstSoundLine = 0;
    GameAudio audio;
    std::size_t sampleLine = 0;
    GameControls controls;
    std::size_t number = 0;
    // Names are checked once every component line has been read, so a
    // description may list components after the systems that use them.
    std::vector<std::pair<std::size_t, std::string>> uses;
    std::vector<std::pair<std::size_t, std::string>> collisionUses;
    bool haveDefault = false;
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
        } else if (kKeyword == "save") {
            // `save <document> ...` for the World; `save player <document>
            // ...` for each player.
            const bool kPlayer = kWords.size() >= 2 && kWords[1] == "player";
            const std::size_t kFirst = kPlayer ? 3 : 2;
            GameSave& save = kPlayer ? game.playerSave : game.save;
            if (!save.document.empty() || kWords.size() <= kFirst) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a game declares one save of each kind, `save [player] <document> <component>...`");
            }
            save.document = kWords[kFirst - 1];
            for (std::size_t index = kFirst; index < kWords.size(); ++index) {
                if (std::ranges::contains(save.components, kWords[index])) {
                    return badLine(number, WorldKestError::BadGameLine, "a save keeps a component once");
                }
                save.components.emplace_back(kWords[index]);
                uses.emplace_back(number, std::string{kWords[index]});
            }
        } else if (kKeyword == "admission") {
            if (admissionLine != 0 || kWords.size() != 2) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a game names one admission rule, `admission <function>`");
            }
            game.admission = kWords[1];
            admissionLine = number;
        } else if (kKeyword == "actions") {
            if (actionsLine != 0 || kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a game names one action set, `actions <file>`");
            }
            controls.actions = kWords[1];
            actionsLine = number;
        } else if (kKeyword == "prefab") {
            const auto kId = kWords.size() == 3 ? parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(number, WorldKestError::BadGameLine, "a prefab line is `prefab <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.prefabs, *kId, &GamePrefab::id) ||
                std::ranges::contains(game.prefabs, kWords[2], &GamePrefab::path)) {
                return badLine(number, WorldKestError::BadGameLine, "a prefab's identity and file are used once");
            }
            game.prefabs.push_back(GamePrefab{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "scene") {
            if (kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a scene line is `scene <file>`");
            }
            if (std::ranges::contains(game.scenes, kWords[1])) {
                return badLine(number, WorldKestError::BadGameLine, "a scene is named once");
            }
            game.scenes.emplace_back(kWords[1]);
        } else if (kKeyword == "mixer") {
            if (mixerLine != 0 || kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a game names one mixer layout, `mixer <file>`");
            }
            audio.mixer = kWords[1];
            mixerLine = number;
        } else if (kKeyword == "sound") {
            const auto kId = kWords.size() == 3 ? parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(number, WorldKestError::BadGameLine, "a sound line is `sound <16 hex digits> <file>`");
            }
            if (std::ranges::contains(audio.sounds, *kId, &GameSound::id)) {
                return badLine(number, WorldKestError::BadGameLine, "a sound's identity is used once");
            }
            audio.sounds.push_back(GameSound{.id = *kId, .path = std::string{kWords[2]}});
            firstSoundLine = firstSoundLine == 0 ? number : firstSoundLine;
        } else if (kKeyword == "sample") {
            if (sampleLine != 0 || kWords.size() != 3) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a game samples its input once, `sample <program> <entry>`");
            }
            controls.program = kWords[1];
            controls.entry = kWords[2];
            sampleLine = number;
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
                if (kWhat == "predicted") {
                    system.predicted = true;
                    --at;
                    continue;
                }
                if (at + 1 == kWords.size()) {
                    return badLine(number, WorldKestError::BadGameLine, "a system column or edge names nothing");
                }
                const std::string kName{kWords[at + 1]};
                if (kWhat == "random") {
                    system.randomStreams.push_back(kName);
                } else if (kWhat == "after") {
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
                                   "edge is after or before a system; a stream is random and its name");
                }
            }
            game.systems.push_back(std::move(system));
        } else if (kKeyword == "replicate" || kKeyword == "player" || kKeyword == "predict" ||
                   kKeyword == "interpolate" || kKeyword == "nearby") {
            std::vector<std::string>& into =
                kKeyword == "replicate"
                    ? game.replicated
                    : (kKeyword == "player"
                           ? game.player
                           : (kKeyword == "predict" ? game.predicted
                                                    : (kKeyword == "nearby" ? game.nearby : game.interpolated)));
            if (kWords.size() < 2) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a replicate, player, predict, nearby, or interpolate line names components");
            }
            for (std::size_t at = 1; at < kWords.size(); ++at) {
                into.emplace_back(kWords[at]);
                uses.emplace_back(number, std::string{kWords[at]});
            }
        } else if (kKeyword == "entity") {
            if (kWords.size() < 3) {
                return badLine(
                    number, WorldKestError::BadGameLine, "an entity line is `entity <component> <field>...`");
            }
            for (std::size_t at = 2; at < kWords.size(); ++at) {
                game.entityFields.push_back(
                    GameEntityField{.component = std::string{kWords[1]}, .field = std::string{kWords[at]}});
            }
            uses.emplace_back(number, std::string{kWords[1]});
        } else if (kKeyword == "physics2d" || kKeyword == "physics3d") {
            // physics2d [gravity <x> <y>] [substeps <n>]
            // physics3d [gravity <x> <y> <z>] [substeps <n>]
            GamePhysics physics{.dimensions = static_cast<std::uint8_t>(kKeyword == "physics3d" ? 3 : 2)};
            bool shaped = !game.physics.has_value();
            std::size_t at = 1;
            const auto kNumber = [&](auto& into) {
                const std::string_view kWord = at < kWords.size() ? kWords[at++] : std::string_view{};
                const auto kRead = std::from_chars(kWord.data(), kWord.data() + kWord.size(), into);
                shaped =
                    shaped && !kWord.empty() && kRead.ec == std::errc{} && kRead.ptr == kWord.data() + kWord.size();
            };
            while (shaped && at < kWords.size()) {
                const std::string_view kWhat = kWords[at++];
                if (kWhat == "gravity") {
                    kNumber(physics.gravityX);
                    kNumber(physics.gravityY);
                    if (physics.dimensions == 3) {
                        kNumber(physics.gravityZ);
                    }
                } else if (kWhat == "substeps") {
                    kNumber(physics.substeps);
                } else {
                    shaped = false;
                }
            }
            if (!shaped) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a game has at most one physics line, `physics2d [gravity <x> <y>] [substeps <n>]` or "
                               "`physics3d [gravity <x> <y> <z>] [substeps <n>]`");
            }
            const PhysicsFacts kFacts = physicsFacts(physics.dimensions);
            for (const physics::ComponentLayout& layout : kFacts.components) {
                game.components.push_back(GameComponent{
                    .id = layout.id, .name = std::string{layout.name}, .kestType = std::string{layout.scriptType}});
            }
            // A contact's entities are references, for checkpoints.
            for (const std::string_view kField : {"hit", "visitor"}) {
                game.entityFields.push_back(
                    GameEntityField{.component = std::string{kFacts.contact}, .field = std::string{kField}});
            }
            game.physics = physics;
        } else if (kKeyword == "collision") {
            const auto kRule = [](std::string_view word) -> std::optional<physics::CollisionRule> {
                if (word == "collide") {
                    return physics::CollisionRule::Collide;
                }
                if (word == "trigger") {
                    return physics::CollisionRule::Trigger;
                }
                if (word == "ignore") {
                    return physics::CollisionRule::Ignore;
                }
                return std::nullopt;
            };
            const std::string_view kWhat = kWords.size() >= 2 ? kWords[1] : std::string_view{};
            std::uint64_t id = 0;
            const auto kHex = [&](std::string_view word) {
                const auto kRead = std::from_chars(word.data(), word.data() + word.size(), id, 16);
                return word.size() == 16 && kRead.ec == std::errc{} && kRead.ptr == word.data() + word.size() &&
                       id != 0;
            };
            if (kWhat == "class" && kWords.size() == 4 && kHex(kWords[3])) {
                game.collision.classes.push_back(GameCollisionClass{.name = std::string{kWords[2]}, .id = id});
            } else if (kWhat == "rule" && kWords.size() == 5 && kRule(kWords[4]).has_value()) {
                game.collision.rules.push_back(GameCollisionRule{
                    .first = std::string{kWords[2]}, .second = std::string{kWords[3]}, .rule = *kRule(kWords[4])});
                collisionUses.emplace_back(number, std::string{kWords[2]});
                collisionUses.emplace_back(number, std::string{kWords[3]});
            } else if (kWhat == "default" && kWords.size() == 3 && kRule(kWords[2]).has_value() && !haveDefault) {
                game.collision.fallback = *kRule(kWords[2]);
                haveDefault = true;
            } else {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a collision line is `collision class <name> <16 hex digits>`, `collision rule <class> "
                               "<class> collide|trigger|ignore`, or one `collision default <rule>`");
            }
        } else if (kKeyword == "interest") {
            // interest <component> <field>... within <radius>
            double radius = 0;
            const bool kShaped = kWords.size() >= 5 && kWords.size() <= 7 && kWords[kWords.size() - 2] == "within";
            const std::string_view kRadius = kShaped ? kWords.back() : std::string_view{};
            const auto kRead = std::from_chars(kRadius.data(), kRadius.data() + kRadius.size(), radius);
            if (!kShaped || game.interest.has_value() || kRead.ec != std::errc{} ||
                kRead.ptr != kRadius.data() + kRadius.size() || !(radius > 0) || !std::isfinite(radius)) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a game has at most one interest line, `interest <component> <field>... within "
                               "<radius>`, with one to three fields and a positive radius");
            }
            GameInterest interest{.component = std::string{kWords[1]}, .axes = {}, .radius = radius};
            for (std::size_t at = 2; at + 2 < kWords.size(); ++at) {
                interest.axes.emplace_back(kWords[at]);
            }
            uses.emplace_back(number, interest.component);
            game.interest = std::move(interest);
        } else if (kKeyword == "input") {
            if (kWords.size() < 2 || kWords.size() > 3 || (kWords.size() == 3 && kWords[2] != "perceived") ||
                !game.input.empty()) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a game names at most one input component, `input <component> [perceived]`");
            }
            game.input = kWords[1];
            uses.emplace_back(number, game.input);
            if (kWords.size() == 3) {
                game.inputPerceived = true;
                game.components.push_back(
                    GameComponent{.id = world_replication::Perception::kComponentTypeId,
                                  .name = std::string{world_replication::Perception::kComponentName},
                                  .kestType = "Perception"});
            }
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
            return badLine(number,
                           WorldKestError::BadGameLine,
                           "a line starts with program, component, system, spawn, replicate, player, or input");
        }
    }
    if (!haveProgram) {
        return badLine(number, WorldKestError::BadGameLine, "a game names exactly one program");
    }
    // Any entity may carry a persistent identity (SPEC-0006), from a scene,
    // a spawn line, or `world.persist`.
    game.components.push_back(GameComponent{.id = world::Persistent::kComponentTypeId,
                                            .name = std::string{world::Persistent::kComponentName},
                                            .kestType = "Persistent"});
    for (const auto& [line, name] : uses) {
        if (!declared(game, name)) {
            return badLine(line, WorldKestError::UnknownName, "a line names a component the game does not declare");
        }
    }
    // Every player holds the moment its client saw.
    if (game.inputPerceived) {
        game.player.emplace_back(world_replication::Perception::kComponentName);
    }
    for (const auto& [line, name] : collisionUses) {
        if (std::ranges::find(game.collision.classes, name, &GameCollisionClass::name) ==
            game.collision.classes.end()) {
            return badLine(
                line, WorldKestError::UnknownName, "a collision rule names a class the game does not declare");
        }
    }
    if ((!game.collision.classes.empty() || haveDefault) && !game.physics.has_value()) {
        return badLine(number, WorldKestError::BadGameLine, "collision lines need a physics line");
    }
    if ((actionsLine == 0) != (sampleLine == 0) || (actionsLine != 0 && game.input.empty())) {
        return badLine(std::max(actionsLine, sampleLine),
                       WorldKestError::BadGameLine,
                       "`actions` and `sample` come together, with an `input` line");
    }
    if (actionsLine != 0) {
        game.controls = std::move(controls);
    }
    if (!game.playerSave.document.empty()) {
        for (const std::string& component : game.playerSave.components) {
            if (!std::ranges::contains(game.player, component)) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a player's save keeps components players start with");
            }
        }
    }
    if (admissionLine != 0 && game.replicated.empty()) {
        return badLine(
            admissionLine, WorldKestError::BadGameLine, "an admission rule needs a networked game, with `replicate`");
    }
    if (firstSoundLine != 0 && mixerLine == 0) {
        return badLine(firstSoundLine, WorldKestError::BadGameLine, "sound lines need a mixer line");
    }
    if (mixerLine != 0) {
        game.audio = std::move(audio);
    }
    return game;
}

std::vector<std::string> sceneNames(const GameDescription& game) {
    std::vector<std::string> names = game.scenes;
    for (const GamePrefab& prefab : game.prefabs) {
        if (!std::ranges::contains(names, prefab.path)) {
            names.push_back(prefab.path);
        }
    }
    return names;
}

} // namespace rawframe::world_kest
