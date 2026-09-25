#include "rawframe/world_kest/game_files.h"

#include "game_files_participant.h"
#include "rawframe/composition/composition.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/scene/scene.h"
#include "rawframe/world_kest/cooked_game.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

namespace rawframe::world_kest {

namespace {

/// The largest description, document, or Kest file a game reads.
constexpr std::uintmax_t kLargestFile = std::uintmax_t{1} << 20U;

std::unexpected<result::Error> unreadable(std::string_view why, std::string_view where) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::NotFound, kWorldKestDomain, code(WorldKestError::UnreadableFile), why)
            .error()
            .withContext("path", where)};
}

result::Result<std::string> readText(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t kSize = std::filesystem::file_size(path, error);
    if (error || kSize > kLargestFile) {
        return unreadable("a game file cannot be read, or is larger than 1 MiB", path.string());
    }
    std::ifstream file{path, std::ios::binary};
    std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    if (!file.is_open() || text.size() != kSize) {
        return unreadable("a game file cannot be read", path.string());
    }
    return text;
}

/// Every `.kest` file under `directory`, by its path relative to it, in path
/// order.
result::Result<std::vector<kest::SourceFile>> kestFilesUnder(const std::filesystem::path& directory) {
    std::vector<kest::SourceFile> files;
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator{directory, error};
         !error && entry != std::filesystem::recursive_directory_iterator{};
         entry.increment(error)) {
        if (!entry->is_regular_file() || entry->path().extension() != ".kest") {
            continue;
        }
        if (files.size() == kest_library::kMaximumGameFiles) {
            return unreadable("a game has more Kest files than a game may", directory.string());
        }
        RAWFRAME_TRY_ASSIGN(std::string text, readText(entry->path()));
        files.push_back(kest::SourceFile{.path = entry->path().lexically_relative(directory).generic_string(),
                                         .text = std::move(text)});
    }
    if (error) {
        return unreadable("a game's directory cannot be listed", directory.string());
    }
    std::ranges::sort(files, {}, &kest::SourceFile::path);
    return files;
}

/// A field of the digest: its length, then its bytes, so no two games read
/// as one.
void field(base::Sha256& digest, std::string_view text) {
    const auto kLength = static_cast<std::uint64_t>(text.size());
    std::array<std::byte, 8> prefix{};
    for (std::size_t at = 0; at < prefix.size(); ++at) {
        prefix[at] = static_cast<std::byte>(kLength >> (8U * (7U - at)));
    }
    digest.update(prefix);
    digest.update(text);
}

/// Every document name the description uses.
std::vector<std::string> documentNames(const GameDescription& description) {
    std::vector<std::string> names;
    if (description.controls) {
        names.push_back(description.controls->actions);
    }
    if (description.audio) {
        names.push_back(description.audio->mixer);
        for (const GameSound& sound : description.audio->sounds) {
            names.push_back(sound.path);
        }
    }
    std::ranges::sort(names);
    const auto kRepeated = std::ranges::unique(names);
    names.erase(kRepeated.begin(), kRepeated.end());
    return names;
}

/// Every program name the description uses.
std::vector<std::string> programNames(const GameDescription& description) {
    std::vector<std::string> names = {description.program};
    if (description.controls && description.controls->program != description.program) {
        names.push_back(description.controls->program);
    }
    return names;
}

std::unexpected<result::Error> invalid(std::string_view why, std::string_view name) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::DataLoss, kWorldKestDomain, code(WorldKestError::CookedGameInvalid), why)
            .error()
            .withContext("name", name)};
}

/// One resource's verified bytes, read and waited for.
result::Result<std::string> readResource(content::ContentStore& store, const content::ResourceRef& reference) {
    RAWFRAME_TRY_ASSIGN(execution::AsyncHandle<content::VerifiedContent> read, store.read(reference));
    // Cancelled only when the Runtime is stopping as it composes.
    RAWFRAME_TRY_ASSIGN(
        const content::VerifiedContent kRead,
        execution::toResult(read.wait(),
                            execution::CancellationMapping{.errorClass = result::ErrorClass::Unavailable,
                                                           .domain = kWorldKestDomain,
                                                           .code = code(WorldKestError::UnreadableFile),
                                                           .description = "a game's read was cancelled"}));
    const std::span<const std::byte> kBytes = kRead.bytes();
    return std::string{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()};
}

} // namespace

void GameFiles::seal() {
    base::Sha256 digest;
    field(digest, "rawframe.world_kest.game_files.v1");
    field(digest, text_);
    for (const Named& document : documents_) {
        field(digest, document.name);
        field(digest, document.text);
    }
    for (const Named& scene : scenes_) {
        field(digest, scene.name);
        field(digest, scene.text);
    }
    for (const auto& [kId, kText] : instanced_) {
        std::array<char, base::kBits128HexDigits> digits{};
        base::formatBits128Hex(kId, digits);
        field(digest, std::string_view{digits.data(), digits.size()});
        field(digest, kText);
    }
    for (const std::vector<kest::SourceFile>& files : sources_) {
        for (const kest::SourceFile& file : files) {
            field(digest, file.path);
            field(digest, file.text);
        }
    }
    digest_ = digest.finish();
}

result::Status GameFiles::readInstanced(const std::function<result::Result<std::string>(base::Bits128)>& read) {
    // Breadth first over every scene's instances; each scene once.
    std::vector<std::string_view> pending;
    for (const Named& scene : scenes_) {
        pending.push_back(scene.text);
    }
    // Reserved whole, so the texts `pending` views never move.
    std::vector<std::pair<base::Bits128, std::string>> found;
    found.reserve(scene::kMaximumInstances);
    while (!pending.empty()) {
        const std::string_view kText = pending.back();
        pending.pop_back();
        RAWFRAME_TRY_ASSIGN(const scene::Scene kScene, scene::readScene(kText));
        for (const scene::SceneInstance& instance : kScene.instances) {
            if (std::ranges::contains(found, instance.scene, &std::pair<base::Bits128, std::string>::first)) {
                continue;
            }
            if (found.size() == scene::kMaximumInstances) {
                return unreadable("a game's scenes instance more than 4,096 scenes", "");
            }
            RAWFRAME_TRY_ASSIGN(std::string text, read(instance.scene));
            found.emplace_back(instance.scene, std::move(text));
            pending.push_back(found.back().second);
        }
    }
    std::ranges::sort(found, {}, &std::pair<base::Bits128, std::string>::first);
    instanced_ = std::move(found);
    return {};
}

result::Result<GameFiles> GameFiles::fromDirectory(const std::filesystem::path& path) {
    GameFiles game;
    game.named_ = true;
    RAWFRAME_TRY_ASSIGN(game.text_, readText(path));
    RAWFRAME_TRY_ASSIGN(game.description_, parseGame(game.text_));
    const std::filesystem::path kDirectory = path.parent_path().empty() ? "." : path.parent_path();
    for (std::string& name : documentNames(game.description_)) {
        RAWFRAME_TRY_ASSIGN(std::string text, readText(kDirectory / name));
        game.documents_.push_back(Named{.name = std::move(name), .text = std::move(text)});
    }
    for (const std::string& name : game.description_.scenes) {
        RAWFRAME_TRY_ASSIGN(std::string text, readText(kDirectory / name));
        game.scenes_.push_back(Named{.name = name, .text = std::move(text)});
    }
    // A scene an instance names, by the sidecar that names it.
    RAWFRAME_TRY(game.readInstanced([&kDirectory](base::Bits128 scene) -> result::Result<std::string> {
        std::error_code error;
        for (auto entry = std::filesystem::recursive_directory_iterator{kDirectory, error};
             !error && entry != std::filesystem::recursive_directory_iterator{};
             entry.increment(error)) {
            const std::string kName = entry->path().filename().string();
            if (!entry->is_regular_file() || !kName.ends_with(content::kSidecarSuffix)) {
                continue;
            }
            RAWFRAME_TRY_ASSIGN(const std::string kText, readText(entry->path()));
            const auto kSidecar = content::readSidecar(kText);
            if (kSidecar.has_value() && kSidecar->importer == "rawframe.scene" && kSidecar->id.value == scene) {
                const std::string kSource = entry->path().string();
                return readText(kSource.substr(0, kSource.size() - content::kSidecarSuffix.size()));
            }
        }
        return unreadable("no scene beside the game has the identity an instance names", "");
    }));
    RAWFRAME_TRY_ASSIGN(std::vector<kest::SourceFile> files, kestFilesUnder(kDirectory));
    game.sources_.push_back(std::move(files));
    for (std::string& name : programNames(game.description_)) {
        game.programs_.push_back(Program{.name = name, .entry = name, .sources = 0});
    }
    game.directory_ = kDirectory;
    game.seal();
    return game;
}

result::Result<GameFiles> GameFiles::fromContent(game_content::GameContent& content, content::ResourceId description) {
    const content::ResourceTypeId kGameType{kCookedGameType};
    const content::ResourceTypeId kSourcesType{kest_library::kGameSourcesType};
    const content::ResourceTypeId kSceneType{scene::kSceneType};
    const std::array<content::AdmittedRepresentation, 3> kAdmitted = {
        content::AdmittedRepresentation{.type = kGameType,
                                        .representation = *content::RepresentationId::parse(kCookedGameRepresentation)},
        content::AdmittedRepresentation{
            .type = kSourcesType,
            .representation = *content::RepresentationId::parse(kest_library::kGameSourcesRepresentation)},
        content::AdmittedRepresentation{
            .type = kSceneType, .representation = *content::RepresentationId::parse(scene::kSceneRepresentation)}};
    RAWFRAME_TRY(content.admit(kAdmitted));
    RAWFRAME_TRY_ASSIGN(const std::string kRecord,
                        readResource(content.store(), content::ResourceRef{.id = description, .type = kGameType}));
    RAWFRAME_TRY_ASSIGN(const CookedGame kCooked, readCookedGame(kRecord));

    GameFiles game;
    game.named_ = true;
    game.text_ = kCooked.text;
    RAWFRAME_TRY_ASSIGN(game.description_, parseGame(game.text_));
    for (std::string& name : documentNames(game.description_)) {
        const CookedGameFile* const kFile = kCooked.file(name);
        if (kFile == nullptr) {
            return invalid("the cooked description does not hold a document it names", name);
        }
        game.documents_.push_back(Named{.name = std::move(name), .text = kFile->text});
    }
    for (const std::string& name : game.description_.scenes) {
        const CookedGameScene* const kScene = kCooked.scene(name);
        if (kScene == nullptr) {
            return invalid("the cooked description does not name the resource of a scene it names", name);
        }
        RAWFRAME_TRY_ASSIGN(
            std::string text,
            readResource(content.store(),
                         content::ResourceRef{.id = content::ResourceId{kScene->scene}, .type = kSceneType}));
        game.scenes_.push_back(Named{.name = name, .text = std::move(text)});
    }
    RAWFRAME_TRY(game.readInstanced([&content, &kSceneType](base::Bits128 scene) {
        return readResource(content.store(),
                            content::ResourceRef{.id = content::ResourceId{scene}, .type = kSceneType});
    }));
    // Each Kest sources resource once, however many programs it holds.
    std::vector<base::Bits128> read;
    for (std::string& name : programNames(game.description_)) {
        const CookedGameProgram* const kProgram = kCooked.program(name);
        if (kProgram == nullptr) {
            return invalid("the cooked description does not name the sources of a program it names", name);
        }
        auto found = std::ranges::find(read, kProgram->sources);
        if (found == read.end()) {
            RAWFRAME_TRY_ASSIGN(
                const std::string kSources,
                readResource(content.store(),
                             content::ResourceRef{.id = content::ResourceId{kProgram->sources}, .type = kSourcesType}));
            RAWFRAME_TRY_ASSIGN(std::vector<kest::SourceFile> files, kest_library::readGameSources(kSources));
            game.sources_.push_back(std::move(files));
            read.push_back(kProgram->sources);
            found = read.end() - 1;
        }
        game.programs_.push_back(Program{.name = std::move(name),
                                         .entry = kProgram->entry,
                                         .sources = static_cast<std::size_t>(found - read.begin())});
    }
    game.seal();
    return game;
}

result::Result<std::string_view> GameFiles::document(std::string_view name) const {
    const auto kFound = std::ranges::find(documents_, name, &Named::name);
    if (kFound == documents_.end()) {
        return unreadable("the description names no such document", name);
    }
    return std::string_view{kFound->text};
}

result::Result<std::string_view> GameFiles::scene(std::string_view name) const {
    const auto kFound = std::ranges::find(scenes_, name, &Named::name);
    if (kFound == scenes_.end()) {
        return unreadable("the description names no such scene", name);
    }
    return std::string_view{kFound->text};
}

result::Result<std::string_view> GameFiles::sceneById(base::Bits128 scene) const {
    const auto kFound = std::ranges::lower_bound(instanced_, scene, {}, &std::pair<base::Bits128, std::string>::first);
    if (kFound == instanced_.end() || kFound->first != scene) {
        return unreadable("no scene of the game's has that identity", "");
    }
    return std::string_view{kFound->second};
}

result::Result<std::shared_ptr<const kest::Program>>
GameFiles::compile(std::string_view name, const kest::CompileSettings& settings, std::string* report) const {
    const auto kProgram = std::ranges::find(programs_, name, &Program::name);
    if (kProgram == programs_.end()) {
        return unreadable("the description names no such program", name);
    }
    if (directory_) {
        RAWFRAME_TRY_ASSIGN(const std::vector<kest::SourceFile> kNow, kestFilesUnder(*directory_));
        return kest_library::compile(kProgram->entry, kNow, settings, report);
    }
    return kest_library::compile(kProgram->entry, sources_[kProgram->sources], settings, report);
}

namespace {

constexpr std::string_view kProvides[] = {kGameFiles.name};
constexpr std::string_view kMaybe[] = {game_content::kGameContent.name};

class GameFilesParticipant final : public composition::Participant {
public:
    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kGameFiles.name) {
            return composition::provideAs<GameFiles>(files);
        }
        return {};
    }

    GameFiles files;
};

result::Result<composition::ParticipantOwner> makeGameFiles(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<GameFilesParticipant>();
    const auto kPath = context.configuration().text("kest.game");
    const auto kResource = context.configuration().text("kest.game_resource");
    if (kPath && kResource) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kWorldKestDomain,
                            code(WorldKestError::UnknownName),
                            "a game is named by kest.game or kest.game_resource, not both");
    }
    if (kPath) {
        RAWFRAME_TRY_ASSIGN(participant->files, GameFiles::fromDirectory(std::string{*kPath}));
    } else if (kResource) {
        const base::Bits128Parse kId = base::parseBits128Hex(*kResource);
        if (!kId.parsed || kId.value == base::Bits128{} || !context.has(game_content::kGameContent.name)) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kWorldKestDomain,
                                code(WorldKestError::UnknownName),
                                "kest.game_resource is a resource identity of the Runtime's content");
        }
        RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
        RAWFRAME_TRY_ASSIGN(participant->files, GameFiles::fromContent(*content, content::ResourceId{kId.value}));
    }
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerGameFiles(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_kest.game_files",
        .factory = &makeGameFiles,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvides,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "world_kest.game_files",
        .budgetOwner = "world",
    });
}

} // namespace rawframe::world_kest
