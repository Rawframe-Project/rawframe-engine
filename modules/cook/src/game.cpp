#include "rawframe/cook/game.h"

#include "rawframe/audio/layout.h"
#include "rawframe/audio/sound.h"
#include "rawframe/cook/errors.h"
#include "rawframe/input/actions.h"
#include "rawframe/kest_library/library.h"
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

/// The Kest sources of the project beside the description, by its sidecar,
/// and their files, read once for every program.
struct Project {
    base::Bits128 sources{};
    std::vector<kest::SourceFile> files;
};

result::Result<Project> projectBeside(Reads& reads, std::string_view program) {
    auto sidecarBytes = reads.file(std::string{"kest.project"} + std::string{content::kSidecarSuffix});
    if (!sidecarBytes.has_value()) {
        return refuse("a program's kest.project is beside the description, with a sidecar", program);
    }
    RAWFRAME_TRY_ASSIGN(const content::Sidecar kSidecar, content::readSidecar(textOf(*sidecarBytes)));
    if (kSidecar.importer != "rawframe.kest") {
        return refuse("the project beside the description is cooked by rawframe.kest", program);
    }
    Project project{.sources = kSidecar.id.value};
    RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kNames, reads.files(".", ".kest"));
    for (const std::string& name : kNames) {
        RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kBytes, reads.file(name));
        project.files.push_back(kest::SourceFile{.path = name, .text = std::string{textOf(kBytes)}});
    }
    return project;
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
    std::optional<Project> project;
    for (const std::string& program : programs) {
        if (game.program(program) != nullptr) {
            continue;
        }
        if (!kest_library::plainGamePath(program)) {
            return refuse("a program is named by a plain path under the description's directory", program);
        }
        if (!project) {
            RAWFRAME_TRY_ASSIGN(Project found, projectBeside(reads, program));
            project = std::move(found);
        }
        std::string report;
        if (!kest_library::compile(program, project->files, {}, &report).has_value()) {
            return std::unexpected<result::Error>{
                refuse("a program does not compile from its sources", program).error().withContext("report", report)};
        }
        game.programs.push_back(
            world_kest::CookedGameProgram{.path = program, .sources = project->sources, .entry = program});
    }

    // Each scene: the resource its sidecar names, cooked by rawframe.scene.
    for (const std::string& path : kDescription.scenes) {
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
