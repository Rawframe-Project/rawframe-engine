#include "rawframe/world_kest/game.h"

#include "mod_api.h"
#include "physics_facts.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_animation/components.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/layouts.h"
#include "rawframe/world_replication/client_worlds.h"
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

/// A finite number within a range, as a word.
std::optional<double> parseReal(std::string_view word, double lowest, double highest) noexcept {
    double value = 0;
    const auto kRead = std::from_chars(word.data(), word.data() + word.size(), value);
    if (kRead.ec != std::errc{} || kRead.ptr != word.data() + word.size() || !std::isfinite(value) || value < lowest ||
        value > highest) {
        return std::nullopt;
    }
    return value;
}

bool lowerSnake(std::string_view word) noexcept {
    return !word.empty() && std::ranges::all_of(word, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
    });
}

/// An `effect <name> predicted|confirmed_only [sound <16 hex digits>]
/// [felt <haptic> <amplitude> <hertz> <milliseconds>]` line's words, or
/// nothing when they are not that.
std::optional<GameEffect> parseEffect(const std::vector<std::string_view>& words) {
    if (words.size() < 3 || !lowerSnake(words[1]) || (words[2] != "predicted" && words[2] != "confirmed_only")) {
        return std::nullopt;
    }
    GameEffect effect{.name = std::string{words[1]},
                      .effectClass = words[2] == "predicted" ? world_replication::EffectClass::Predicted
                                                             : world_replication::EffectClass::ConfirmedOnly};
    std::size_t at = 3;
    if (at + 1 < words.size() && words[at] == "sound") {
        const auto kSound = parseHex64(words[at + 1]);
        if (!kSound.has_value() || *kSound == 0) {
            return std::nullopt;
        }
        effect.sound = *kSound;
        at += 2;
    }
    if (at + 5 == words.size() && words[at] == "felt") {
        const auto kAmplitude = parseReal(words[at + 2], 0, 1);
        const auto kFrequency = parseReal(words[at + 3], 0, 1000);
        const auto kMilliseconds = parseReal(words[at + 4], 1, 5000);
        if (!lowerSnake(words[at + 1]) || !kAmplitude.has_value() || *kAmplitude == 0 || !kFrequency.has_value() ||
            !kMilliseconds.has_value() || *kMilliseconds != std::floor(*kMilliseconds)) {
            return std::nullopt;
        }
        effect.felt = GameFelt{.haptic = std::string{words[at + 1]},
                               .amplitude = static_cast<float>(*kAmplitude),
                               .frequency = static_cast<float>(*kFrequency),
                               .milliseconds = static_cast<std::uint32_t>(*kMilliseconds)};
        at += 5;
    }
    return at == words.size() ? std::optional{std::move(effect)} : std::nullopt;
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
    ModLines modLines;
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
        if (modKeyword(kKeyword)) {
            RAWFRAME_TRY(readModLine(kWords, number, game, modLines));
        } else if (kKeyword == "program") {
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
        } else if (kKeyword == "mesh") {
            const auto kId = kWords.size() == 3 ? parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(number, WorldKestError::BadGameLine, "a mesh line is `mesh <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.meshes, *kId, &GameMesh::id) ||
                std::ranges::contains(game.meshes, kWords[2], &GameMesh::path)) {
                return badLine(number, WorldKestError::BadGameLine, "a mesh's identity and file are used once");
            }
            game.meshes.push_back(GameMesh{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "texture") {
            const auto kId = kWords.size() == 3 ? parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a texture line is `texture <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.textures, *kId, &GameTexture::id) ||
                std::ranges::contains(game.textures, kWords[2], &GameTexture::path) ||
                std::ranges::contains(game.renderTextures, *kId, &GameRenderTexture::id)) {
                return badLine(number, WorldKestError::BadGameLine, "a texture's identity and file are used once");
            }
            game.textures.push_back(GameTexture{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "rendertexture") {
            const bool kShaped = kWords.size() == 4 || kWords.size() == 5;
            const auto kId = kShaped ? parseHex64(kWords[1]) : std::nullopt;
            const auto kWidth = kShaped ? parseReal(kWords[2], 1, kMaximumRenderTextureSide) : std::nullopt;
            const auto kHeight = kShaped ? parseReal(kWords[3], 1, kMaximumRenderTextureSide) : std::nullopt;
            const std::string_view kUpdate = kWords.size() == 5 ? kWords[4] : std::string_view{"every_frame"};
            if (!kId || *kId == 0 || !kWidth || !kHeight || std::floor(*kWidth) != *kWidth ||
                std::floor(*kHeight) != *kHeight || (kUpdate != "every_frame" && kUpdate != "on_demand")) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a render texture line is `rendertexture <16 hex digits> <width> <height> "
                               "[every_frame | on_demand]`, each side 1 to 4096 pixels");
            }
            if (std::ranges::contains(game.renderTextures, *kId, &GameRenderTexture::id) ||
                std::ranges::contains(game.textures, *kId, &GameTexture::id)) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a render texture's identity is no other texture's");
            }
            if (game.renderTextures.size() == kMaximumRenderTextures) {
                return badLine(number, WorldKestError::BadGameLine, "a game declares at most 8 render textures");
            }
            game.renderTextures.push_back(GameRenderTexture{
                .id = *kId,
                .width = static_cast<std::uint32_t>(*kWidth),
                .height = static_cast<std::uint32_t>(*kHeight),
                .update = kUpdate == "on_demand" ? RenderTextureUpdate::OnDemand : RenderTextureUpdate::EveryFrame});
        } else if (kKeyword == "layout") {
            // layout <players> then <x> <y> <width> <height> for each player
            const auto kPlayers =
                kWords.size() >= 2 ? parseReal(kWords[1], 2, world_replication::kMaximumLocalPlayers) : std::nullopt;
            GameLayout layout;
            bool shaped = kPlayers.has_value() && std::floor(*kPlayers) == *kPlayers &&
                          kWords.size() == 2 + (4 * static_cast<std::size_t>(*kPlayers));
            for (std::size_t at = 2; shaped && at + 3 < kWords.size(); at += 4) {
                const auto kX = parseReal(kWords[at], 0, 1);
                const auto kY = parseReal(kWords[at + 1], 0, 1);
                const auto kWidth = parseReal(kWords[at + 2], 0, 1);
                const auto kHeight = parseReal(kWords[at + 3], 0, 1);
                shaped = kX && kY && kWidth && kHeight && *kWidth > 0 && *kHeight > 0 && *kX + *kWidth <= 1 &&
                         *kY + *kHeight <= 1;
                if (shaped) {
                    layout.regions.push_back(GameRegion{.x = static_cast<float>(*kX),
                                                        .y = static_cast<float>(*kY),
                                                        .width = static_cast<float>(*kWidth),
                                                        .height = static_cast<float>(*kHeight)});
                }
            }
            if (!shaped) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a layout line is `layout <players>` then `<x> <y> <width> <height>` for each, 2 to 4 "
                               "players, each region inside the window");
            }
            layout.players = static_cast<std::uint32_t>(*kPlayers);
            if (std::ranges::contains(game.layouts, layout.players, &GameLayout::players)) {
                return badLine(number, WorldKestError::BadGameLine, "a game lays out a count of players once");
            }
            game.layouts.push_back(std::move(layout));
        } else if (kKeyword == "material") {
            const auto kId = kWords.size() == 3 ? parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a material line is `material <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.materials, *kId, &GameMaterial::id) ||
                std::ranges::contains(game.materials, kWords[2], &GameMaterial::path)) {
                return badLine(number, WorldKestError::BadGameLine, "a material's identity and file are used once");
            }
            game.materials.push_back(GameMaterial{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "animator") {
            // animator <16 hex digits> <graph file> [parameters <component>]
            //     [subset <32 hex digits>]
            constexpr std::string_view kShape = "an animator line is `animator <16 hex digits> <graph file> "
                                                "[parameters <component>] [subset <32 hex digits>]`";
            const auto kId = kWords.size() >= 3 && kWords.size() % 2 == 1 ? parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(number, WorldKestError::BadGameLine, kShape);
            }
            if (std::ranges::contains(game.animators, *kId, &GameAnimator::id) ||
                std::ranges::contains(game.animators, kWords[2], &GameAnimator::path)) {
                return badLine(number, WorldKestError::BadGameLine, "an animator's identity and graph are used once");
            }
            GameAnimator animator{.id = *kId, .path = std::string{kWords[2]}, .parameters = {}, .subset = {}};
            // Each option once, in this order.
            std::size_t at = 3;
            if (at < kWords.size() && kWords[at] == "parameters") {
                animator.parameters = kWords[at + 1];
                uses.emplace_back(number, animator.parameters);
                at += 2;
            }
            if (at < kWords.size() && kWords[at] == "subset") {
                const base::Bits128Parse kSubset = base::parseBits128Hex(kWords[at + 1]);
                if (!kSubset.parsed || kSubset.value == base::Bits128{}) {
                    return badLine(number, WorldKestError::BadGameLine, kShape);
                }
                animator.subset = kSubset.value;
                at += 2;
            }
            if (at != kWords.size()) {
                return badLine(number, WorldKestError::BadGameLine, kShape);
            }
            game.animators.push_back(std::move(animator));
        } else if (kKeyword == "scene") {
            if (kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a scene line is `scene <file>`");
            }
            if (std::ranges::contains(game.scenes, kWords[1])) {
                return badLine(number, WorldKestError::BadGameLine, "a scene is named once");
            }
            game.scenes.emplace_back(kWords[1]);
        } else if (kKeyword == "text") {
            if (kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a text line is `text <file>`");
            }
            if (std::ranges::contains(game.texts, kWords[1])) {
                return badLine(number, WorldKestError::BadGameLine, "a text document is named once");
            }
            game.texts.emplace_back(kWords[1]);
        } else if (kKeyword == "locale") {
            if (!game.locale.empty() || kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a game names one default locale, `locale <tag>`");
            }
            game.locale = kWords[1];
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
                } else if (kWhat == "emits") {
                    system.emits.push_back(kName);
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
                                   "edge is after or before a system; a stream is random and its name; an effect "
                                   "is emits and its name");
                }
            }
            game.systems.push_back(std::move(system));
        } else if (kKeyword == "present") {
            if (kWords.size() < 3) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a present line is `present <identity> <entry>` then columns");
            }
            GameSystem system{.identity = std::string{kWords[1]}, .entry = std::string{kWords[2]}};
            for (std::size_t at = 3; at < kWords.size(); at += 2) {
                if (kWords[at] == "entities") {
                    system.columns.push_back(
                        GameColumn{.access = world::Access::Read, .component = {}, .entities = true});
                    --at;
                    continue;
                }
                const auto kAccess = accessNamed(kWords[at]);
                if (!kAccess.has_value() || at + 1 == kWords.size()) {
                    return badLine(number,
                                   WorldKestError::BadGameLine,
                                   "a present system's column is entities, or read, write, with, or without a "
                                   "component");
                }
                system.columns.push_back(GameColumn{.access = *kAccess, .component = std::string{kWords[at + 1]}});
                uses.emplace_back(number, std::string{kWords[at + 1]});
            }
            if (std::ranges::contains(game.presented, system.identity, &GameSystem::identity)) {
                return badLine(number, WorldKestError::BadGameLine, "a present system's identity is its own");
            }
            game.presented.push_back(std::move(system));
        } else if (kKeyword == "presentation") {
            const auto kOn = std::ranges::find(kWords, std::string_view{"on"});
            if (kWords.size() < 4 || kOn != kWords.end() - 2 || kOn == kWords.begin() + 1) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a presentation line is `presentation <component>... on <component or player>`");
            }
            GamePresentation presentation;
            if (kWords.back() != "player") {
                presentation.on = std::string{kWords.back()};
                uses.emplace_back(number, *presentation.on);
            }
            for (auto word = kWords.begin() + 1; word != kOn; ++word) {
                presentation.components.emplace_back(*word);
                uses.emplace_back(number, std::string{*word});
            }
            game.presentation.push_back(std::move(presentation));
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
            for (const schema::ComponentLayout& layout : kFacts.components) {
                game.components.push_back(GameComponent{
                    .id = layout.id, .name = std::string{layout.name}, .kestType = std::string{layout.scriptType}});
            }
            // A contact's entities are references, for checkpoints.
            for (const std::string_view kField : {"hit", "visitor"}) {
                game.entityFields.push_back(
                    GameEntityField{.component = std::string{kFacts.contact}, .field = std::string{kField}});
            }
            // As are the bodies a joint holds, and an attachment's parent.
            for (const std::string_view kField : {"a", "b"}) {
                game.entityFields.push_back(
                    GameEntityField{.component = std::string{kFacts.joint}, .field = std::string{kField}});
            }
            game.entityFields.push_back(GameEntityField{.component = std::string{kFacts.attach}, .field = "parent"});
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
        } else if (kKeyword == "effect") {
            std::optional<GameEffect> effect = parseEffect(kWords);
            if (!effect.has_value() || std::ranges::contains(game.effects, effect->name, &GameEffect::name) ||
                game.effects.size() >= kMaximumEffects) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "an effect line is `effect <lower_snake name> predicted|confirmed_only`, then "
                               "optionally `sound <16 hex digits>`, then optionally `felt <haptic> <amplitude> "
                               "<hertz> <milliseconds>`, each name once, at most 64");
            }
            game.effects.push_back(std::move(*effect));
        } else if (kKeyword == "message") {
            if (kWords.size() != 3 || !lowerSnake(kWords[1]) ||
                std::ranges::contains(game.messages, kWords[1], &GameMessage::name) ||
                game.messages.size() >= kMaximumMessages) {
                return badLine(
                    number,
                    WorldKestError::BadGameLine,
                    "a message line is `message <lower_snake name> <Kest type>`, each name once, at most 64");
            }
            game.messages.push_back(GameMessage{.name = std::string{kWords[1]}, .kestType = std::string{kWords[2]}});
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
    // Animators play on the engine's components: the Animator, named by
    // their identity, and root motion.
    if (!game.animators.empty()) {
        for (const schema::ComponentLayout& layout : world_animation::componentLayouts()) {
            game.components.push_back(GameComponent{
                .id = layout.id, .name = std::string{layout.name}, .kestType = std::string{layout.scriptType}});
        }
    }
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
    // An effect's sound is one the game declares (D220).
    for (const GameEffect& effect : game.effects) {
        if (effect.sound != 0 &&
            (!game.audio.has_value() || !std::ranges::contains(game.audio->sounds, effect.sound, &GameSound::id))) {
            return badLine(number, WorldKestError::UnknownName, "an effect's sound is one the game declares");
        }
    }
    // Each effect has one emitter, a predicted system (D219).
    std::vector<std::string_view> emitted;
    for (const GameSystem& system : game.systems) {
        for (const std::string& effect : system.emits) {
            if (!std::ranges::contains(game.effects, effect, &GameEffect::name) || !system.predicted ||
                std::ranges::contains(emitted, effect)) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a system emits a declared effect, is predicted, and is the effect's one emitter");
            }
            emitted.push_back(effect);
        }
    }
    // Presentation state is a client's alone (D260): no server carries,
    // sends, predicts, or runs a system over it, and only present systems
    // write it.
    std::vector<std::string_view> presentational;
    for (const GamePresentation& presentation : game.presentation) {
        for (const std::string& component : presentation.components) {
            if (std::ranges::contains(presentational, component) || component == presentation.on) {
                return badLine(number, WorldKestError::BadGameLine, "a component is presentation state once");
            }
            presentational.push_back(component);
        }
    }
    for (const std::string_view kComponent : presentational) {
        const bool kCarried =
            std::ranges::contains(game.replicated, kComponent) || std::ranges::contains(game.player, kComponent) ||
            std::ranges::contains(game.predicted, kComponent) || std::ranges::contains(game.interpolated, kComponent) ||
            std::ranges::any_of(game.systems, [&](const GameSystem& system) {
                return std::ranges::contains(system.columns, kComponent, &GameColumn::component);
            });
        if (kCarried) {
            return badLine(number,
                           WorldKestError::BadGameLine,
                           "presentation state is not replicated, predicted, held by players, or used by a system");
        }
    }
    for (const GameSystem& system : game.presented) {
        for (const GameColumn& column : system.columns) {
            if (column.access == world::Access::Write && !std::ranges::contains(presentational, column.component)) {
                return badLine(number, WorldKestError::BadGameLine, "a present system writes only presentation state");
            }
        }
    }
    RAWFRAME_TRY(checkModApi(game, modLines));
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

std::string spawnValue(const GameDescription& game, std::string_view component, const GameFieldValue& value) {
    if (value.field == "collisionClass" &&
        (component == physics2d::Body2D::kComponentName || component == physics3d::Body3D::kComponentName)) {
        const auto kClass = std::ranges::find(game.collision.classes, value.value, &GameCollisionClass::name);
        if (kClass != game.collision.classes.end()) {
            return std::to_string(kClass->id);
        }
    }
    if (value.field == "mesh" && component == physics3d::Mesh3D::kComponentName) {
        const auto kMesh = std::ranges::find(game.meshes, value.value, &GameMesh::path);
        if (kMesh != game.meshes.end()) {
            return std::to_string(kMesh->id);
        }
    }
    if (value.field == "texture") {
        const auto kComponent = std::ranges::find(game.components, component, &GameComponent::name);
        const auto kTexture = std::ranges::find(game.textures, value.value, &GameTexture::path);
        if (kComponent != game.components.end() && ofEngineType(*kComponent, "rawframe.canvas.Sprite") &&
            kTexture != game.textures.end()) {
            return std::to_string(kTexture->id);
        }
    }
    if (value.field == "material") {
        const auto kComponent = std::ranges::find(game.components, component, &GameComponent::name);
        const auto kMaterial = std::ranges::find(game.materials, value.value, &GameMaterial::path);
        if (kComponent != game.components.end() && ofEngineType(*kComponent, "rawframe.model.Model") &&
            kMaterial != game.materials.end()) {
            return std::to_string(kMaterial->id);
        }
    }
    if (value.field == "graph" && component == world_animation::Animator::kComponentName) {
        const auto kAnimator = std::ranges::find(game.animators, value.value, &GameAnimator::path);
        if (kAnimator != game.animators.end()) {
            return std::to_string(kAnimator->id);
        }
    }
    return value.value;
}

RegionPixels pixelsOf(const GameRegion& region, std::uint32_t width, std::uint32_t height) noexcept {
    const auto kEdge = [](float at, std::uint32_t side) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(at, 0.0F, 1.0F) * static_cast<float>(side)));
    };
    const std::uint32_t kLeft = kEdge(region.x, width);
    const std::uint32_t kTop = kEdge(region.y, height);
    const std::uint32_t kRight = std::max(kLeft, kEdge(region.x + region.width, width));
    const std::uint32_t kBottom = std::max(kTop, kEdge(region.y + region.height, height));
    return RegionPixels{.x = kLeft, .y = kTop, .width = kRight - kLeft, .height = kBottom - kTop};
}

} // namespace rawframe::world_kest
