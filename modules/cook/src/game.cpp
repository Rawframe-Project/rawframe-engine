#include "rawframe/cook/game.h"

#include "project.h"
#include "rawframe/animation/resources.h"
#include "rawframe/audio/layout.h"
#include "rawframe/audio/sound.h"
#include "rawframe/cook/errors.h"
#include "rawframe/input/actions.h"
#include "rawframe/localization/catalog.h"
#include "rawframe/world_kest/cooked_game.h"
#include "rawframe/world_kest/game.h"

#include <algorithm>

namespace rawframe::cook {

namespace {

std::unexpected<result::Error> refuse(std::string_view why, std::string_view name) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadReference), why)
            .error()
            .withContext("name", name)};
}

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           kCookDomain,
                                                           code(CookError::BadSidecar),
                                                           "a game description takes no settings")
                                                  .error()};
    }
    return std::string{};
}

/// A document the description names, read and kept by that name.
result::Result<std::string_view> keep(Reads& reads, world_kest::CookedGame& game, std::string_view path) {
    RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kBytes, reads.file(path));
    if (game.file(path) == nullptr) {
        game.files.push_back(
            world_kest::CookedGameFile{.path = std::string{path}, .text = std::string{textOf(kBytes)}});
    }
    return textOf(kBytes);
}

result::Result<Artifact> cookGame(std::span<const std::byte> source, std::string_view, Reads& reads) {
    const std::string_view kText = textOf(source);
    RAWFRAME_TRY_ASSIGN(const world_kest::GameDescription kDescription, world_kest::parseGame(kText));
    world_kest::CookedGame game{.text = std::string{kText}};

    // Each program: an entry among the project's sources that compiles.
    std::vector<std::string> programs = {kDescription.program};
    if (kDescription.controls) {
        programs.push_back(kDescription.controls->program);
    }
    std::optional<KestProject> project;
    for (const std::string& program : programs) {
        if (game.program(program) != nullptr) {
            continue;
        }
        if (!project) {
            RAWFRAME_TRY_ASSIGN(KestProject found, projectBeside(reads, program));
            project = std::move(found);
        }
        RAWFRAME_TRY(compiles(*project, program));
        game.programs.push_back(
            world_kest::CookedGameProgram{.path = program, .sources = project->sources, .entry = program});
    }

    // Each scene: the resource its sidecar names, cooked by rawframe.scene.
    for (const std::string& path : world_kest::sceneNames(kDescription)) {
        auto sidecarBytes = reads.file(path + std::string{content::kSidecarSuffix});
        if (!sidecarBytes.has_value()) {
            return refuse("a scene the description names has a sidecar", path);
        }
        RAWFRAME_TRY_ASSIGN(const content::Sidecar kSidecar, content::readSidecar(textOf(*sidecarBytes)));
        if (kSidecar.importer != "rawframe.scene") {
            return refuse("a scene the description names is cooked by rawframe.scene", path);
        }
        game.scenes.push_back(world_kest::CookedGameScene{.path = path, .scene = kSidecar.id.value});
    }

    // Each mesh: the resource its sidecar names, cooked by rawframe.mesh.
    for (const world_kest::GameMesh& mesh : kDescription.meshes) {
        auto sidecarBytes = reads.file(mesh.path + std::string{content::kSidecarSuffix});
        if (!sidecarBytes.has_value()) {
            return refuse("a mesh the description names has a sidecar", mesh.path);
        }
        RAWFRAME_TRY_ASSIGN(const content::Sidecar kSidecar, content::readSidecar(textOf(*sidecarBytes)));
        if (kSidecar.importer != "rawframe.mesh") {
            return refuse("a mesh the description names is cooked by rawframe.mesh", mesh.path);
        }
        game.meshes.push_back(world_kest::CookedGameMesh{.path = mesh.path, .mesh = kSidecar.id.value});
    }

    // Each animator's graph: the resource its sidecar names, cooked by
    // rawframe.animation from a graph document. Its clips and their
    // skeleton are resources the graph names, cooked from their own
    // sidecars.
    for (const world_kest::GameAnimator& animator : kDescription.animators) {
        auto sidecarBytes = reads.file(animator.path + std::string{content::kSidecarSuffix});
        auto graphBytes = reads.file(animator.path);
        if (!sidecarBytes.has_value() || !graphBytes.has_value()) {
            return refuse("an animator's graph the description names has a sidecar", animator.path);
        }
        RAWFRAME_TRY_ASSIGN(const content::Sidecar kSidecar, content::readSidecar(textOf(*sidecarBytes)));
        if (kSidecar.importer != "rawframe.animation" ||
            animation::documentKind(textOf(*graphBytes)) != animation::DocumentKind::Graph) {
            return refuse("an animator's graph is a graph document cooked by rawframe.animation", animator.path);
        }
        game.animators.push_back(world_kest::CookedGameAnimator{.path = animator.path, .graph = kSidecar.id.value});
    }

    // Each text document: the resource its sidecar names, cooked by
    // rawframe.text. Together they must make a catalog (D147), so an
    // orphaned key or a translation reading an argument its source does not
    // fails here rather than on a client.
    std::vector<localization::TableDocument> tables;
    std::vector<localization::Translations> translations;
    for (const std::string& path : kDescription.texts) {
        auto sidecarBytes = reads.file(path + std::string{content::kSidecarSuffix});
        auto textBytes = reads.file(path);
        if (!sidecarBytes.has_value() || !textBytes.has_value()) {
            return refuse("a text document the description names has a sidecar", path);
        }
        RAWFRAME_TRY_ASSIGN(const content::Sidecar kSidecar, content::readSidecar(textOf(*sidecarBytes)));
        if (kSidecar.importer != "rawframe.text") {
            return refuse("a text document the description names is cooked by rawframe.text", path);
        }
        // A table is read as one; anything else must read as a translation.
        auto table = localization::readStrings(textOf(*textBytes));
        if (table.has_value()) {
            tables.push_back(localization::TableDocument{.id = kSidecar.id.value, .table = std::move(*table)});
        } else {
            auto translated = localization::readTranslations(textOf(*textBytes));
            if (!translated.has_value()) {
                return refuse("a text document reads as a string table or a translation", path);
            }
            translations.push_back(std::move(*translated));
        }
        game.texts.push_back(world_kest::CookedGameText{.path = path, .document = kSidecar.id.value});
    }
    if (!kDescription.texts.empty()) {
        auto catalog = localization::Catalog::build(tables, translations);
        if (!catalog.has_value()) {
            return std::unexpected<result::Error>{
                std::move(catalog).error().mappedTo(result::ErrorClass::InvalidArgument,
                                                    kCookDomain,
                                                    code(CookError::BadReference),
                                                    "the text documents a game names make a catalog")};
        }
    }
    if (!kDescription.locale.empty()) {
        const auto kLocale = localization::parseLocale(kDescription.locale);
        if (!kLocale.has_value() || !localization::intake(kDescription.locale).has_value() ||
            *localization::intake(kDescription.locale) != *kLocale) {
            return refuse("a game's default locale is a canonical tag CLDR knows", kDescription.locale);
        }
    }

    // Each document, read as its owner reads it.
    if (kDescription.controls) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kActions, keep(reads, game, kDescription.controls->actions));
        if (!input::readActionSet(kActions).has_value()) {
            return refuse("an actions document does not read", kDescription.controls->actions);
        }
    }
    if (kDescription.audio) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kMixer, keep(reads, game, kDescription.audio->mixer));
        const auto kLayout = audio::readLayout(kMixer);
        if (!kLayout.has_value()) {
            return refuse("a mixer document does not read", kDescription.audio->mixer);
        }
        for (const world_kest::GameSound& sound : kDescription.audio->sounds) {
            RAWFRAME_TRY_ASSIGN(const std::string_view kSound, keep(reads, game, sound.path));
            if (!audio::readSound(kSound, *kLayout).has_value()) {
                return refuse("a sound document does not read against the mixer", sound.path);
            }
        }
    }

    RAWFRAME_TRY_ASSIGN(const std::string kWritten, world_kest::writeCookedGame(game));
    const auto kBytes = std::as_bytes(std::span{kWritten.data(), kWritten.size()});
    return Artifact{.type = content::ResourceTypeId{world_kest::kCookedGameType},
                    .representation = *content::RepresentationId::parse(world_kest::kCookedGameRepresentation),
                    .bytes = {kBytes.begin(), kBytes.end()}};
}

} // namespace

Importer gameImporter() noexcept {
    return Importer{.identity = "rawframe.game", .normalize = &normalize, .cook = &cookGame};
}

} // namespace rawframe::cook
