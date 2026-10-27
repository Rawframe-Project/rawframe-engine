#include "rawframe/world_kest/game.h"

#include "description_words.h"
#include "field_hints.h"
#include "mod_api.h"
#include "navigation.h"
#include "physics_facts.h"
#include "rawframe/base/hex64.h"
#include "rawframe/world/layer.h"
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
        const auto kSound = base::parseHex64(words[at + 1]);
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

/// Whether a system writes a component it looks up or scans: its own
/// writes are journaled until its run ends, so the lookup would read what
/// was there before them.
bool writesALookup(const GameSystem& system) {
    return std::ranges::any_of(system.columns, [&system](const GameColumn& column) {
        return column.access == world::Access::Write && (std::ranges::contains(system.lookups, column.component) ||
                                                         std::ranges::contains(system.scans, column.component));
    });
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
    std::size_t navigationLine = 0;
    std::size_t mixerLine = 0;
    std::size_t firstSoundLine = 0;
    GameAudio audio;
    std::size_t sampleLine = 0;
    GameControls controls;
    std::size_t number = 0;
    ModLines modLines;
    std::vector<std::size_t> labelLines;
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
        } else if (kKeyword == "styles") {
            if (!game.styles.empty() || kWords.size() != 2) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a game names one styles document, `styles <file>`");
            }
            game.styles = kWords[1];
        } else if (kKeyword == "actions") {
            if (actionsLine != 0 || kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a game names one action set, `actions <file>`");
            }
            controls.actions = kWords[1];
            actionsLine = number;
        } else if (kKeyword == "prefab") {
            const auto kId = kWords.size() == 3 ? base::parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(number, WorldKestError::BadGameLine, "a prefab line is `prefab <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.prefabs, *kId, &GamePrefab::id) ||
                std::ranges::contains(game.prefabs, kWords[2], &GamePrefab::path)) {
                return badLine(number, WorldKestError::BadGameLine, "a prefab's identity and file are used once");
            }
            game.prefabs.push_back(GamePrefab{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "mesh") {
            const auto kId = kWords.size() == 3 ? base::parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(number, WorldKestError::BadGameLine, "a mesh line is `mesh <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.meshes, *kId, &GameMesh::id) ||
                std::ranges::contains(game.meshes, kWords[2], &GameMesh::path)) {
                return badLine(number, WorldKestError::BadGameLine, "a mesh's identity and file are used once");
            }
            game.meshes.push_back(GameMesh{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "texture") {
            const auto kId = kWords.size() == 3 ? base::parseHex64(kWords[1]) : std::nullopt;
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
        } else if (kKeyword == "tilemap") {
            const auto kId = kWords.size() == 3 ? base::parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a tilemap line is `tilemap <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.tilemaps, *kId, &GameTilemap::id) ||
                std::ranges::contains(game.tilemaps, kWords[2], &GameTilemap::path)) {
                return badLine(number, WorldKestError::BadGameLine, "a tilemap's identity and file are used once");
            }
            game.tilemaps.push_back(GameTilemap{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "sequence") {
            const auto kId = kWords.size() == 3 ? base::parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(
                    number, WorldKestError::BadGameLine, "a sequence line is `sequence <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.sequences, *kId, &GameSequence::id) ||
                std::ranges::contains(game.sequences, kWords[2], &GameSequence::path)) {
                return badLine(number, WorldKestError::BadGameLine, "a sequence's identity and file are used once");
            }
            game.sequences.push_back(GameSequence{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "rendertexture") {
            const bool kShaped = kWords.size() == 4 || kWords.size() == 5;
            const auto kId = kShaped ? base::parseHex64(kWords[1]) : std::nullopt;
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
        } else if (kKeyword == "aspect") {
            // aspect <width> <height> [<red> <green> <blue>]
            const bool kShaped = (kWords.size() == 3 || kWords.size() == 6) && !game.aspect.has_value();
            const auto kWhole = [&](std::size_t at, double least, double most) {
                const auto kValue = kShaped ? parseReal(kWords[at], least, most) : std::nullopt;
                return kValue.has_value() && std::floor(*kValue) == *kValue ? kValue : std::nullopt;
            };
            const auto kWidth = kWhole(1, 1, 10000);
            const auto kHeight = kWhole(2, 1, 10000);
            GameAspect aspect;
            bool colored = true;
            for (std::size_t at = 0; kWords.size() == 6 && at < 3; ++at) {
                const auto kChannel = kWhole(3 + at, 0, 255);
                colored = colored && kChannel.has_value();
                aspect.bars[at] = kChannel.has_value() ? static_cast<std::uint8_t>(*kChannel) : 0;
            }
            if (!kWidth || !kHeight || !colored) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "an aspect line is `aspect <width> <height> [<red> <green> <blue>]`, whole numbers "
                               "from 1 to 10000 and the bars' color from 0 to 255, once");
            }
            aspect.width = static_cast<std::uint32_t>(*kWidth);
            aspect.height = static_cast<std::uint32_t>(*kHeight);
            game.aspect = aspect;
        } else if (kKeyword == "material") {
            const auto kId = kWords.size() == 3 ? base::parseHex64(kWords[1]) : std::nullopt;
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
            const auto kId = kWords.size() >= 3 && kWords.size() % 2 == 1 ? base::parseHex64(kWords[1]) : std::nullopt;
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
        } else if (kKeyword == "scatter") {
            if (kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a scatter line is `scatter <file>`");
            }
            if (std::ranges::contains(game.scatters, kWords[1])) {
                return badLine(number, WorldKestError::BadGameLine, "a scatter is named once");
            }
            game.scatters.emplace_back(kWords[1]);
        } else if (kKeyword == "text") {
            if (kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a text line is `text <file>`");
            }
            if (std::ranges::contains(game.texts, kWords[1])) {
                return badLine(number, WorldKestError::BadGameLine, "a text document is named once");
            }
            game.texts.emplace_back(kWords[1]);
        } else if (kKeyword == "font") {
            const auto kId = kWords.size() == 3 ? base::parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(number, WorldKestError::BadGameLine, "a font line is `font <16 hex digits> <file>`");
            }
            if (std::ranges::contains(game.fonts, *kId, &GameFont::id) ||
                std::ranges::contains(game.fonts, kWords[2], &GameFont::path)) {
                return badLine(number, WorldKestError::BadGameLine, "a font's identity and file are used once");
            }
            game.fonts.push_back(GameFont{.id = *kId, .path = std::string{kWords[2]}});
        } else if (kKeyword == "label") {
            const auto kId = kWords.size() >= 4 && kWords.size() <= 6 ? base::parseHex64(kWords[1]) : std::nullopt;
            if (!kId || *kId == 0) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a label line is `label <16 hex digits> <table> <key> [<argument> [<argument>]]`");
            }
            if (kWords.size() == 6 && kWords[4] == kWords[5]) {
                return badLine(number, WorldKestError::BadGameLine, "a label's arguments have two names");
            }
            if (std::ranges::contains(game.labels, *kId, &GameLabel::id)) {
                return badLine(number, WorldKestError::BadGameLine, "a label's identity is used once");
            }
            labelLines.push_back(number);
            game.labels.push_back(GameLabel{.id = *kId,
                                            .table = std::string{kWords[2]},
                                            .key = std::string{kWords[3]},
                                            .arguments = {kWords.begin() + 4, kWords.end()}});
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
            const auto kId = kWords.size() == 3 ? base::parseHex64(kWords[1]) : std::nullopt;
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
                } else if (kWhat == "lookup") {
                    system.lookups.push_back(kName);
                    uses.emplace_back(number, kName);
                } else if (kWhat == "scan") {
                    system.scans.push_back(kName);
                    uses.emplace_back(number, kName);
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
                                   "is emits and its name; a lookup is lookup and a component, and a scan scan and "
                                   "a component");
                }
            }
            if (writesALookup(system)) {
                return badLine(number, WorldKestError::BadGameLine, "a system looks up or scans a component it writes");
            }
            // A predicting client runs a predicted system over its player
            // alone, with nothing to scan.
            if (system.predicted && !system.scans.empty()) {
                return badLine(number, WorldKestError::BadGameLine, "a predicted system scans nothing");
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
                if (kWords[at] == "lookup" && at + 1 < kWords.size()) {
                    system.lookups.emplace_back(kWords[at + 1]);
                    uses.emplace_back(number, std::string{kWords[at + 1]});
                    continue;
                }
                const auto kAccess = accessNamed(kWords[at]);
                if (!kAccess.has_value() || at + 1 == kWords.size()) {
                    return badLine(number,
                                   WorldKestError::BadGameLine,
                                   "a present system's column is entities, or read, write, with, or without a "
                                   "component; a lookup is lookup and a component");
                }
                system.columns.push_back(GameColumn{.access = *kAccess, .component = std::string{kWords[at + 1]}});
                uses.emplace_back(number, std::string{kWords[at + 1]});
            }
            if (writesALookup(system)) {
                return badLine(number, WorldKestError::BadGameLine, "a present system looks up a component it writes");
            }
            if (std::ranges::contains(game.presented, system.identity, &GameSystem::identity)) {
                return badLine(number, WorldKestError::BadGameLine, "a present system's identity is its own");
            }
            game.presented.push_back(std::move(system));
        } else if (kKeyword == "ui") {
            if (!game.ui.empty() || kWords.size() != 2) {
                return badLine(number, WorldKestError::BadGameLine, "a game names one UI document, `ui <file>`");
            }
            game.ui = kWords[1];
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
                   kKeyword == "interpolate" || kKeyword == "nearby" || kKeyword == "own") {
            std::vector<std::string>& into =
                kKeyword == "replicate"
                    ? game.replicated
                    : (kKeyword == "player"
                           ? game.player
                           : (kKeyword == "predict"
                                  ? game.predicted
                                  : (kKeyword == "nearby" ? game.nearby
                                                          : (kKeyword == "own" ? game.own : game.interpolated))));
            if (kWords.size() < 2) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a replicate, player, predict, nearby, interpolate, or own line names components");
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
        } else if (kKeyword == "field") {
            GameFieldHint hint;
            if (const auto kWhy = readFieldLine(std::span{kWords}.subspan(1), hint); kWhy.has_value()) {
                return badLine(number, WorldKestError::BadGameLine, *kWhy);
            }
            uses.emplace_back(number, hint.component);
            game.fieldHints.push_back(std::move(hint));
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
        } else if (kKeyword == "navigation") {
            GameNavigation navigation;
            if (game.navigation.has_value() || !readNavigationLine(std::span{kWords}.subspan(1), navigation)) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a game has at most one navigation line, `navigation [radius <m>] [height <m>] [step "
                               "<m>] [slope <degrees>] [cell <m>]`, each value above nought");
            }
            game.navigation = navigation;
            navigationLine = number;
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
        } else if (kKeyword == "command") {
            if (kWords.size() != 3 || !lowerSnake(kWords[1]) ||
                std::ranges::contains(game.commands, kWords[1], &GameCommand::name) ||
                game.commands.size() >= kMaximumCommands) {
                return badLine(
                    number,
                    WorldKestError::BadGameLine,
                    "a command line is `command <lower_snake name> <Kest type>`, each name once, at most 64");
            }
            game.commands.push_back(GameCommand{.name = std::string{kWords[1]}, .kestType = std::string{kWords[2]}});
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
        } else if (kKeyword == "mapped") {
            std::size_t count = 0;
            const auto kCount = kWords.size() == 2
                                    ? std::from_chars(kWords[1].data(), kWords[1].data() + kWords[1].size(), count)
                                    : std::from_chars_result{nullptr, std::errc::invalid_argument};
            if (kCount.ec != std::errc{} || kCount.ptr != kWords[1].data() + kWords[1].size() ||
                count <= world_replication::kMappedByDefault || count > world_replication::kMostMapped ||
                game.mapped != world_replication::kMappedByDefault) {
                return badLine(number,
                               WorldKestError::BadGameLine,
                               "a game has at most one mapped line, `mapped <count>`, past 4096 and at most 65536");
            }
            game.mapped = count;
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
    // Any entity may carry a persistent identity, from a scene,
    // a spawn line, or `world.persist`.
    game.components.push_back(GameComponent{.id = world::Persistent::kComponentTypeId,
                                            .name = std::string{world::Persistent::kComponentName},
                                            .kestType = "Persistent"});
    // And be put on a layer, which an editor groups it by.
    game.components.push_back(GameComponent{
        .id = world::Layer::kComponentTypeId, .name = std::string{world::Layer::kComponentName}, .kestType = "Layer"});
    // A game that navigates has agents to steer round each other.
    if (game.navigation.has_value()) {
        game.components.push_back(GameComponent{.id = NavigationAgent::kComponentTypeId,
                                                .name = std::string{NavigationAgent::kComponentName},
                                                .kestType = "Agent"});
    }
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
    if (navigationLine != 0 && (!game.physics.has_value() || game.physics->dimensions != 3)) {
        return badLine(navigationLine, WorldKestError::BadGameLine, "a navigation line needs a physics3d line");
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
    // An effect's sound is one the game declares.
    for (const GameEffect& effect : game.effects) {
        if (effect.sound != 0 &&
            (!game.audio.has_value() || !std::ranges::contains(game.audio->sounds, effect.sound, &GameSound::id))) {
            return badLine(number, WorldKestError::UnknownName, "an effect's sound is one the game declares");
        }
    }
    // Each effect has one emitter, a predicted system.
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
    // Presentation state is a client's alone: no server carries,
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
    // A label's table is one the game's text comes from.
    for (std::size_t at = 0; at < game.labels.size(); ++at) {
        if (!std::ranges::contains(game.texts, game.labels[at].table)) {
            return badLine(labelLines[at], WorldKestError::UnknownName, "a label's table is one a text line names");
        }
    }
    RAWFRAME_TRY(checkModApi(game, modLines));
    return game;
}

std::string pathBeside(std::string_view document, std::string_view named) {
    const std::size_t kSlash = document.rfind('/');
    const std::string kJoined =
        (kSlash == std::string_view::npos ? std::string{} : std::string{document.substr(0, kSlash + 1)}) +
        std::string{named};
    std::vector<std::string_view> parts;
    std::size_t at = 0;
    const std::string_view kWhole = kJoined;
    while (at <= kWhole.size()) {
        const std::size_t kEnd = std::min(kWhole.find('/', at), kWhole.size());
        const std::string_view kPart = kWhole.substr(at, kEnd - at);
        if (kPart == ".." && !parts.empty() && parts.back() != "..") {
            parts.pop_back();
        } else if (!kPart.empty() && kPart != ".") {
            parts.push_back(kPart);
        }
        at = kEnd + 1;
    }
    std::string made;
    for (const std::string_view kPart : parts) {
        made += (made.empty() ? "" : "/") + std::string{kPart};
    }
    return made;
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
    // A tilemap by its file, for a tiles body and for a canvas's tiles.
    if (value.field == "tilemap") {
        const auto kComponent = std::ranges::find(game.components, component, &GameComponent::name);
        const auto kTilemap = std::ranges::find(game.tilemaps, value.value, &GameTilemap::path);
        if (kTilemap != game.tilemaps.end() &&
            (component == physics2d::Tiles2D::kComponentName ||
             (kComponent != game.components.end() && ofEngineType(*kComponent, "rawframe.canvas.Tilemap")))) {
            return std::to_string(kTilemap->id);
        }
    }
    // A sequence by its file, for a player of it.
    if (value.field == "sequence") {
        const auto kComponent = std::ranges::find(game.components, component, &GameComponent::name);
        const auto kSequence = std::ranges::find(game.sequences, value.value, &GameSequence::path);
        if (kSequence != game.sequences.end() && kComponent != game.components.end() &&
            ofEngineType(*kComponent, "rawframe.sequence.Player")) {
            return std::to_string(kSequence->id);
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

} // namespace rawframe::world_kest
