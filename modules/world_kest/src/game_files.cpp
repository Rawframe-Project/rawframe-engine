#include "rawframe/world_kest/game_files.h"

#include "game_files_participant.h"
#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/mask.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/composition/composition.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/document/json.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/scene/scene.h"
#include "rawframe/world_kest/cooked_game.h"
#include "rawframe/world_kest/cooked_mod.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/mod.h"

#include <algorithm>
#include <array>
#include <iterator>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#endif

namespace rawframe::world_kest {

namespace {

/// The most clips and skeletons a game's animators reach.
constexpr std::size_t kMaximumAnimationDocuments = 4096;

std::unexpected<result::Error> unreadable(std::string_view why, std::string_view where) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::NotFound, kWorldKestDomain, code(WorldKestError::UnreadableFile), why)
            .error()
            .withContext("path", where)};
}

/// The largest description, document, or Kest file a game reads.
constexpr std::size_t kLargestFile = std::size_t{1} << 20U;

} // namespace

struct GameFiles::Reader {
    /// A file by its path relative to the game's directory, at most
    /// kLargestFile bytes.
    std::function<result::Result<std::string>(std::string_view path)> read;
    std::function<bool(std::string_view path)> exists;
    /// Every file's relative path, `/` between parts.
    std::function<result::Result<std::vector<std::string>>()> list;
};

namespace {

/// Sources by the identity their sidecars give them, as relative paths.
using Sources = std::vector<std::pair<base::Bits128, std::string>>;

/// The source of each sidecar the reader lists that `importer` cooks, by
/// the identity it gives it. A sidecar that does not read is no one's.
template <typename Reader> result::Result<Sources> sourcesBySidecar(const Reader& reader, std::string_view importer) {
    Sources sources;
    RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kPaths, reader.list());
    for (const std::string& path : kPaths) {
        if (!path.ends_with(content::kSidecarSuffix)) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const std::string kText, reader.read(path));
        const auto kSidecar = content::readSidecar(kText);
        if (kSidecar.has_value() && kSidecar->importer == importer) {
            sources.emplace_back(kSidecar->id.value, path.substr(0, path.size() - content::kSidecarSuffix.size()));
        }
    }
    return sources;
}

/// Every `.kest` file the reader lists, by its relative path, in path order.
template <typename Reader> result::Result<std::vector<kest::SourceFile>> kestFilesOf(const Reader& reader) {
    std::vector<kest::SourceFile> files;
    RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kPaths, reader.list());
    for (const std::string& path : kPaths) {
        if (!path.ends_with(".kest")) {
            continue;
        }
        if (files.size() == kest_library::kMaximumGameFiles) {
            return unreadable("a game has more Kest files than a game may", path);
        }
        RAWFRAME_TRY_ASSIGN(std::string text, reader.read(path));
        files.push_back(kest::SourceFile{.path = path, .text = std::move(text)});
    }
    std::ranges::sort(files, {}, &kest::SourceFile::path);
    return files;
}

#if RAWFRAME_FILE_SYSTEM
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

/// Every regular file under `directory`, by its path relative to it.
result::Result<std::vector<std::string>> filesUnder(const std::filesystem::path& directory) {
    std::vector<std::string> paths;
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator{directory, error};
         !error && entry != std::filesystem::recursive_directory_iterator{};
         entry.increment(error)) {
        if (entry->is_regular_file()) {
            paths.push_back(entry->path().lexically_relative(directory).generic_string());
        }
    }
    if (error) {
        return unreadable("a game's directory cannot be listed", directory.string());
    }
    return paths;
}
#endif

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

std::string hexOf(base::Bits128 id) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(id, digits);
    return std::string{digits.data(), digits.size()};
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
    for (std::size_t at = 0; at < meshes_.size(); ++at) {
        field(digest, description_.meshes[at].path);
        digest.update(meshDigests_[at]);
    }
    for (const GameText& text : texts_) {
        field(digest, text.path);
        field(digest, hexOf(text.document));
    }
    for (const Named& graph : graphs_) {
        field(digest, graph.name);
        field(digest, graph.text);
    }
    for (const auto& [kId, kText] : animations_) {
        field(digest, hexOf(kId));
        field(digest, kText);
    }
    for (const std::vector<kest::SourceFile>& files : sources_) {
        for (const kest::SourceFile& file : files) {
            field(digest, file.path);
            field(digest, file.text);
        }
    }
    for (const GameModScene& scene : modScenes_) {
        field(digest, scene.mod);
        field(digest, scene.point);
        field(digest, scene.text);
    }
    for (const GameModProgram& program : modPrograms_) {
        field(digest, program.mod);
        field(digest, program.entry);
        for (const kest::SourceFile& file : program.files) {
            field(digest, file.path);
            field(digest, file.text);
        }
        for (const ModHandler& handler : program.handlers) {
            field(digest, handler.point);
            field(digest, handler.function);
        }
        for (const ModProvider& provider : program.providers) {
            field(digest, "provide");
            field(digest, provider.point);
            field(digest, provider.function);
        }
        for (const ModReplacement& replacement : program.replacements) {
            field(digest, "replace");
            field(digest, replacement.point);
            field(digest, replacement.function);
        }
    }
    digest_ = digest.finish();
}

namespace {

/// The name of every component an entity of `text`, a scene, holds.
void componentsOf(std::string_view text, std::vector<std::string>& held) {
    const auto kScene = document::parse(text);
    const document::Value* entities = kScene.has_value() ? kScene->find("entities") : nullptr;
    if (entities == nullptr || entities->kind() != document::Value::Kind::Array) {
        return;
    }
    for (const document::Value& entity : entities->items()) {
        const document::Value* components = entity.find("components");
        if (components == nullptr || components->kind() != document::Value::Kind::Object) {
            continue;
        }
        for (const std::string& name : components->names()) {
            if (std::ranges::find(held, name) == held.end()) {
                held.push_back(name);
            }
        }
    }
}

std::unexpected<result::Error> modRefused(std::string_view why, std::string_view mod) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(WorldKestError::ModRefused), why)
            .error()
            .withContext("mod", mod)};
}

} // namespace

result::Status GameFiles::readMods(game_content::GameContent* content) {
    std::vector<std::string> held;
    for (const Named& scene : scenes_) {
        componentsOf(scene.text, held);
    }
    for (const auto& [kId, kText] : instanced_) {
        componentsOf(kText, held);
    }
    const game_content::ComposedBuild* game = content != nullptr ? content->composedGame() : nullptr;
    if (game == nullptr) {
        return checkMods(description_, "", {}, held);
    }
    const content::ResourceTypeId kModType{kCookedModType};
    const content::ResourceTypeId kSceneType{scene::kSceneType};
    const std::array<content::AdmittedRepresentation, 1> kAdmitted = {content::AdmittedRepresentation{
        .type = kModType, .representation = *content::RepresentationId::parse(kCookedModRepresentation)}};
    std::vector<ComposedMod> mods;
    std::vector<CookedMod> cooked;
    for (const game_content::ComposedBuild& build : content->composedMods()) {
        if (mods.empty()) {
            RAWFRAME_TRY(content->admit(kAdmitted));
        }
        // A mod's Build holds its one description.
        const auto kIsMod = [&kModType](const content::ManifestEntry& entry) {
            return entry.type == kModType;
        };
        const auto kEntry = std::ranges::find_if(build.entries, kIsMod);
        if (kEntry == build.entries.end() || std::ranges::count_if(build.entries, kIsMod) != 1) {
            return modRefused("a mod's Build holds one mod description", build.reference.subject);
        }
        RAWFRAME_TRY_ASSIGN(const std::string kRecord,
                            readResource(content->store(), content::ResourceRef{.id = kEntry->id, .type = kModType}));
        RAWFRAME_TRY_ASSIGN(CookedMod read, readCookedMod(kRecord));
        RAWFRAME_TRY_ASSIGN(ModDescription description, parseMod(read.text));
        mods.push_back(ComposedMod{.subject = build.reference.subject, .description = std::move(description)});
        cooked.push_back(std::move(read));
    }
    RAWFRAME_TRY(checkMods(description_, game->reference.subject, mods, held));
    for (const game_content::ComposedBuild& build : content->composedMods()) {
        modBuilds_.push_back(build.reference);
    }
    // Taken: each contributed scene, as the mod's record names it.
    for (std::size_t at = 0; at < mods.size(); ++at) {
        for (const ModContribution& contribution : mods[at].description.contributions) {
            const CookedGameScene* const kScene = cooked[at].scene(contribution.scene);
            if (kScene == nullptr) {
                return modRefused("a cooked mod does not name the resource of a scene it contributes",
                                  mods[at].subject);
            }
            RAWFRAME_TRY_ASSIGN(
                std::string text,
                readResource(content->store(),
                             content::ResourceRef{.id = content::ResourceId{kScene->scene}, .type = kSceneType}));
            modScenes_.push_back(GameModScene{.mod = mods[at].subject,
                                              .point = contribution.point,
                                              .text = std::move(text),
                                              .identity = kScene->scene});
        }
        // The program its functions are in, from the mod's own sources.
        if (mods[at].description.program.empty()) {
            continue;
        }
        if (cooked[at].programs.size() != 1 || cooked[at].programs[0].path != mods[at].description.program) {
            return modRefused("a cooked mod does not name the sources of the program its functions are in",
                              mods[at].subject);
        }
        const content::ResourceTypeId kSourcesType{kest_library::kGameSourcesType};
        RAWFRAME_TRY_ASSIGN(const std::string kSources,
                            readResource(content->store(),
                                         content::ResourceRef{.id = content::ResourceId{cooked[at].programs[0].sources},
                                                              .type = kSourcesType}));
        RAWFRAME_TRY_ASSIGN(std::vector<kest::SourceFile> files, kest_library::readGameSources(kSources));
        modPrograms_.push_back(GameModProgram{.mod = mods[at].subject,
                                              .entry = cooked[at].programs[0].entry,
                                              .files = std::move(files),
                                              .handlers = mods[at].description.handlers,
                                              .providers = mods[at].description.providers,
                                              .replacements = mods[at].description.replacements});
    }
    return {};
}

result::Result<std::shared_ptr<const kest::Program>>
GameFiles::compileMod(const GameModProgram& program, const kest::CompileSettings& settings, std::string* report) {
    return kest_library::compile(program.entry, program.files, settings, report);
}

result::Status GameFiles::readMeshes(game_content::GameContent* content, const std::vector<base::Bits128>& resources) {
    if (description_.meshes.empty()) {
        return {};
    }
    if (content == nullptr) {
        return unreadable("a game's meshes are read cooked, from the Runtime's content",
                          description_.meshes.front().path);
    }
    const content::ResourceTypeId kMeshType{mesh::kMeshType};
    const std::array<content::AdmittedRepresentation, 1> kAdmitted = {content::AdmittedRepresentation{
        .type = kMeshType, .representation = *content::RepresentationId::parse(mesh::kMeshRepresentation)}};
    RAWFRAME_TRY(content->admit(kAdmitted));
    for (std::size_t at = 0; at < resources.size(); ++at) {
        const GameMesh& declared = description_.meshes[at];
        auto bytes = readResource(content->store(),
                                  content::ResourceRef{.id = content::ResourceId{resources[at]}, .type = kMeshType});
        if (!bytes.has_value()) {
            return std::unexpected<result::Error>{std::move(bytes).error().withContext("name", declared.path)};
        }
        const std::span<const std::byte> kBytes = std::as_bytes(std::span{bytes->data(), bytes->size()});
        auto decoded = mesh::decode(kBytes);
        if (!decoded.has_value()) {
            return std::unexpected<result::Error>{std::move(decoded).error().withContext("name", declared.path)};
        }
        meshes_.push_back(
            physics3d::BodyMesh{.id = declared.id, .mesh = std::make_shared<const mesh::Mesh>(std::move(*decoded))});
        meshDigests_.push_back(base::sha256(kBytes));
    }
    return {};
}

result::Status GameFiles::readAnimations(
    const std::function<result::Result<std::string>(base::Bits128, animation::DocumentKind)>& read) {
    std::vector<std::pair<base::Bits128, std::string>> found;
    // Reads `id` as a document of `kind` unless it has been, and says
    // whether it was new.
    const auto kReadOnce = [&found, &read](base::Bits128 id, animation::DocumentKind kind) -> result::Result<bool> {
        if (std::ranges::contains(found, id, &std::pair<base::Bits128, std::string>::first)) {
            return false;
        }
        if (found.size() == kMaximumAnimationDocuments) {
            return unreadable("a game's animators reach more than 4,096 clips and skeletons", "");
        }
        RAWFRAME_TRY_ASSIGN(std::string text, read(id, kind));
        if (animation::documentKind(text) != kind) {
            return unreadable("an animation document is not of the kind that names it expects", hexOf(id));
        }
        found.emplace_back(id, std::move(text));
        return true;
    };
    for (const Named& graph : graphs_) {
        auto parsed = animation::readGraph(graph.text);
        if (!parsed.has_value()) {
            return std::unexpected<result::Error>{std::move(parsed).error().withContext("path", graph.name)};
        }
        for (const base::Bits128 kMask : animation::masksOf(*parsed)) {
            RAWFRAME_TRY_ASSIGN(const bool kNew, kReadOnce(kMask, animation::DocumentKind::Mask));
            if (kNew) {
                RAWFRAME_TRY(animation::readMask(found.back().second));
            }
        }
        for (const base::Bits128 kClip : animation::clipsOf(*parsed)) {
            RAWFRAME_TRY_ASSIGN(const bool kNew, kReadOnce(kClip, animation::DocumentKind::Clip));
            if (!kNew) {
                continue;
            }
            RAWFRAME_TRY_ASSIGN(const animation::Clip kRead, animation::readClip(found.back().second));
            if (kRead.skeleton.has_value()) {
                RAWFRAME_TRY(kReadOnce(*kRead.skeleton, animation::DocumentKind::Skeleton));
            }
        }
    }
    // Each animator's server subset, a mask its graph need not name.
    for (const GameAnimator& animator : description_.animators) {
        if (!animator.subset.has_value()) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const bool kNew, kReadOnce(*animator.subset, animation::DocumentKind::Mask));
        if (kNew) {
            RAWFRAME_TRY(animation::readMask(found.back().second));
        }
    }
    std::ranges::sort(found, {}, &std::pair<base::Bits128, std::string>::first);
    animations_ = std::move(found);
    return {};
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

result::Result<GameFiles>
GameFiles::fromReader(std::string_view description, const Reader& reader, game_content::GameContent* content) {
    GameFiles game;
    game.named_ = true;
    RAWFRAME_TRY_ASSIGN(game.text_, reader.read(description));
    RAWFRAME_TRY_ASSIGN(game.description_, parseGame(game.text_));
    for (std::string& name : documentNames(game.description_)) {
        RAWFRAME_TRY_ASSIGN(std::string text, reader.read(name));
        game.documents_.push_back(Named{.name = std::move(name), .text = std::move(text)});
    }
    for (const std::string& name : sceneNames(game.description_)) {
        RAWFRAME_TRY_ASSIGN(std::string text, reader.read(name));
        std::optional<base::Bits128> identity;
        const std::string kSidecar = name + std::string{content::kSidecarSuffix};
        if (reader.exists(kSidecar)) {
            RAWFRAME_TRY_ASSIGN(const std::string kSidecarText, reader.read(kSidecar));
            const auto kRead = content::readSidecar(kSidecarText);
            if (kRead.has_value() && kRead->importer == "rawframe.scene") {
                identity = kRead->id.value;
            }
        }
        game.scenes_.push_back(Named{.name = name, .text = std::move(text), .identity = identity});
    }
    // A scene an instance names, and an animator's clip or skeleton, by the
    // sidecar that names it; the directory is listed only for a game that
    // asks.
    const auto kBySidecar = [&reader](std::string_view importer, std::string_view what) {
        return [&reader, importer, what, sources = std::optional<Sources>{}](
                   base::Bits128 id) mutable -> result::Result<std::string> {
            if (!sources.has_value()) {
                RAWFRAME_TRY_ASSIGN(sources, sourcesBySidecar(reader, importer));
            }
            const auto kFound = std::ranges::find(*sources, id, &Sources::value_type::first);
            if (kFound == sources->end()) {
                return unreadable(what, hexOf(id));
            }
            return reader.read(kFound->second);
        };
    };
    RAWFRAME_TRY(game.readInstanced(
        kBySidecar("rawframe.scene", "no scene beside the game has the identity an instance names")));
    for (const GameAnimator& animator : game.description_.animators) {
        RAWFRAME_TRY_ASSIGN(std::string text, reader.read(animator.path));
        game.graphs_.push_back(Named{.name = animator.path, .text = std::move(text)});
    }
    RAWFRAME_TRY(game.readAnimations(
        [animations = kBySidecar("rawframe.animation",
                                 "no clip or skeleton beside the game has the identity an animator reaches")](
            base::Bits128 id, animation::DocumentKind) mutable {
            return animations(id);
        }));
    // Each mesh, by the resource its sidecar names.
    std::vector<base::Bits128> meshes;
    for (const GameMesh& declared : game.description_.meshes) {
        const auto kSidecarText = reader.read(declared.path + std::string{content::kSidecarSuffix});
        const auto kSidecarRead =
            kSidecarText.has_value() ? std::optional{content::readSidecar(*kSidecarText)} : std::nullopt;
        if (!kSidecarRead.has_value() || !kSidecarRead->has_value() || (*kSidecarRead)->importer != "rawframe.mesh") {
            return unreadable("a mesh the game names has a sidecar naming rawframe.mesh", declared.path);
        }
        meshes.push_back((*kSidecarRead)->id.value);
    }
    RAWFRAME_TRY(game.readMeshes(content, meshes));
    // Each text document, by the resource its sidecar names.
    for (const std::string& text : game.description_.texts) {
        const auto kSidecarText = reader.read(text + std::string{content::kSidecarSuffix});
        const auto kSidecarRead =
            kSidecarText.has_value() ? std::optional{content::readSidecar(*kSidecarText)} : std::nullopt;
        if (!kSidecarRead.has_value() || !kSidecarRead->has_value() || (*kSidecarRead)->importer != "rawframe.text") {
            return unreadable("a text document the game names has a sidecar naming rawframe.text", text);
        }
        game.texts_.push_back(GameText{.path = text, .document = (*kSidecarRead)->id.value});
    }
    RAWFRAME_TRY_ASSIGN(std::vector<kest::SourceFile> files, kestFilesOf(reader));
    game.sources_.push_back(std::move(files));
    for (std::string& name : programNames(game.description_)) {
        game.programs_.push_back(Program{.name = name, .entry = name, .sources = 0});
    }
    RAWFRAME_TRY(game.readMods(nullptr));
    game.seal();
    return game;
}

#if RAWFRAME_FILE_SYSTEM
result::Result<GameFiles> GameFiles::fromDirectory(const std::filesystem::path& path,
                                                   game_content::GameContent* content) {
    const std::filesystem::path kDirectory = path.parent_path().empty() ? "." : path.parent_path();
    const Reader kReader{.read =
                             [&kDirectory](std::string_view relative) {
                                 return readText(kDirectory / std::string{relative});
                             },
                         .exists =
                             [&kDirectory](std::string_view relative) {
                                 return std::filesystem::is_regular_file(kDirectory / std::string{relative});
                             },
                         .list =
                             [&kDirectory] {
                                 return filesUnder(kDirectory);
                             }};
    RAWFRAME_TRY_ASSIGN(GameFiles game, fromReader(path.filename().string(), kReader, content));
    game.directory_ = kDirectory;
    return game;
}
#endif

result::Result<GameFiles> GameFiles::fromHeld(std::string_view description,
                                              std::vector<std::pair<std::string, std::string>> files,
                                              game_content::GameContent* content) {
    std::ranges::sort(files, {}, &std::pair<std::string, std::string>::first);
    const auto kFind = [&files](std::string_view path) {
        const auto kAt = std::ranges::lower_bound(files, path, {}, &std::pair<std::string, std::string>::first);
        return kAt != files.end() && kAt->first == path ? &*kAt : nullptr;
    };
    const Reader kReader{.read = [&kFind](std::string_view path) -> result::Result<std::string> {
                             const auto* file = kFind(path);
                             if (file == nullptr || file->second.size() > kLargestFile) {
                                 return unreadable("a game file is not held, or is larger than 1 MiB", path);
                             }
                             return file->second;
                         },
                         .exists =
                             [&kFind](std::string_view path) {
                                 return kFind(path) != nullptr;
                             },
                         .list = [&files]() -> result::Result<std::vector<std::string>> {
                             std::vector<std::string> paths;
                             for (const auto& file : files) {
                                 paths.push_back(file.first);
                             }
                             return paths;
                         }};
    return fromReader(description, kReader, content);
}

result::Result<GameFiles> GameFiles::fromContent(game_content::GameContent& content, content::ResourceId description) {
    const content::ResourceTypeId kGameType{kCookedGameType};
    const content::ResourceTypeId kSourcesType{kest_library::kGameSourcesType};
    const content::ResourceTypeId kSceneType{scene::kSceneType};
    const content::ResourceTypeId kGraphType{animation::kGraphType};
    const content::ResourceTypeId kClipType{animation::kClipType};
    const content::ResourceTypeId kSkeletonType{animation::kSkeletonType};
    const content::ResourceTypeId kMaskType{animation::kMaskType};
    const std::array<content::AdmittedRepresentation, 7> kAdmitted = {
        content::AdmittedRepresentation{.type = kGameType,
                                        .representation = *content::RepresentationId::parse(kCookedGameRepresentation)},
        content::AdmittedRepresentation{
            .type = kSourcesType,
            .representation = *content::RepresentationId::parse(kest_library::kGameSourcesRepresentation)},
        content::AdmittedRepresentation{
            .type = kSceneType, .representation = *content::RepresentationId::parse(scene::kSceneRepresentation)},
        content::AdmittedRepresentation{
            .type = kGraphType, .representation = *content::RepresentationId::parse(animation::kGraphRepresentation)},
        content::AdmittedRepresentation{
            .type = kClipType, .representation = *content::RepresentationId::parse(animation::kClipRepresentation)},
        content::AdmittedRepresentation{.type = kSkeletonType,
                                        .representation =
                                            *content::RepresentationId::parse(animation::kSkeletonRepresentation)},
        content::AdmittedRepresentation{
            .type = kMaskType, .representation = *content::RepresentationId::parse(animation::kMaskRepresentation)}};
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
    for (const std::string& name : sceneNames(game.description_)) {
        const CookedGameScene* const kScene = kCooked.scene(name);
        if (kScene == nullptr) {
            return invalid("the cooked description does not name the resource of a scene it names", name);
        }
        RAWFRAME_TRY_ASSIGN(
            std::string text,
            readResource(content.store(),
                         content::ResourceRef{.id = content::ResourceId{kScene->scene}, .type = kSceneType}));
        game.scenes_.push_back(Named{.name = name, .text = std::move(text), .identity = kScene->scene});
    }
    RAWFRAME_TRY(game.readInstanced([&content, &kSceneType](base::Bits128 scene) {
        return readResource(content.store(),
                            content::ResourceRef{.id = content::ResourceId{scene}, .type = kSceneType});
    }));
    std::vector<base::Bits128> meshes;
    for (const GameMesh& declared : game.description_.meshes) {
        const CookedGameMesh* const kMesh = kCooked.mesh(declared.path);
        if (kMesh == nullptr) {
            return invalid("the cooked description does not name the resource of a mesh it names", declared.path);
        }
        meshes.push_back(kMesh->mesh);
    }
    RAWFRAME_TRY(game.readMeshes(&content, meshes));
    for (const std::string& path : game.description_.texts) {
        const CookedGameText* const kText = kCooked.textDocument(path);
        if (kText == nullptr) {
            return invalid("the cooked description does not name the resource of a text document it names", path);
        }
        game.texts_.push_back(GameText{.path = path, .document = kText->document});
    }
    for (const GameAnimator& animator : game.description_.animators) {
        const CookedGameAnimator* const kAnimator = kCooked.animator(animator.path);
        if (kAnimator == nullptr) {
            return invalid("the cooked description does not name the resource of a graph it names", animator.path);
        }
        RAWFRAME_TRY_ASSIGN(
            std::string text,
            readResource(content.store(),
                         content::ResourceRef{.id = content::ResourceId{kAnimator->graph}, .type = kGraphType}));
        game.graphs_.push_back(Named{.name = animator.path, .text = std::move(text), .identity = kAnimator->graph});
    }
    RAWFRAME_TRY(game.readAnimations(
        [&content, &kClipType, &kSkeletonType, &kMaskType](base::Bits128 id, animation::DocumentKind kind) {
            const content::ResourceTypeId kType = kind == animation::DocumentKind::Clip   ? kClipType
                                                  : kind == animation::DocumentKind::Mask ? kMaskType
                                                                                          : kSkeletonType;
            return readResource(content.store(), content::ResourceRef{.id = content::ResourceId{id}, .type = kType});
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
    RAWFRAME_TRY(game.readMods(&content));
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

std::optional<base::Bits128> GameFiles::sceneIdentity(std::string_view name) const {
    const auto kFound = std::ranges::find(scenes_, name, &Named::name);
    return kFound != scenes_.end() ? kFound->identity : std::nullopt;
}

result::Result<std::string_view> GameFiles::sceneById(base::Bits128 scene) const {
    const auto kFound = std::ranges::lower_bound(instanced_, scene, {}, &std::pair<base::Bits128, std::string>::first);
    if (kFound == instanced_.end() || kFound->first != scene) {
        return unreadable("no scene of the game's has that identity", "");
    }
    return std::string_view{kFound->second};
}

result::Result<std::string_view> GameFiles::animatorGraph(std::string_view path) const {
    const auto kFound = std::ranges::find(graphs_, path, &Named::name);
    if (kFound == graphs_.end()) {
        return unreadable("no animator of the game's names that graph", path);
    }
    return std::string_view{kFound->text};
}

result::Result<std::string_view> GameFiles::animationDocument(base::Bits128 id) const {
    const auto kFound = std::ranges::lower_bound(animations_, id, {}, &std::pair<base::Bits128, std::string>::first);
    if (kFound == animations_.end() || kFound->first != id) {
        return unreadable("no clip or skeleton the game's animators reach has that identity", "");
    }
    return std::string_view{kFound->second};
}

result::Result<std::shared_ptr<const kest::Program>>
GameFiles::compile(std::string_view name, const kest::CompileSettings& settings, std::string* report) const {
    const auto kProgram = std::ranges::find(programs_, name, &Program::name);
    if (kProgram == programs_.end()) {
        return unreadable("the description names no such program", name);
    }
#if RAWFRAME_FILE_SYSTEM
    if (directory_) {
        const std::filesystem::path& directory = *directory_;
        const Reader kReader{.read =
                                 [&directory](std::string_view relative) {
                                     return readText(directory / std::string{relative});
                                 },
                             .exists = {},
                             .list =
                                 [&directory] {
                                     return filesUnder(directory);
                                 }};
        RAWFRAME_TRY_ASSIGN(const std::vector<kest::SourceFile> kNow, kestFilesOf(kReader));
        return kest_library::compile(kProgram->entry, kNow, settings, report);
    }
#endif
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
    const auto kPath = context.configuration().path("kest.game");
    const auto kResource = context.configuration().text("kest.game_resource");
    if (kPath && kResource) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kWorldKestDomain,
                            code(WorldKestError::UnknownName),
                            "a game is named by kest.game or kest.game_resource, not both");
    }
    if (kPath) {
        game_content::GameContent* content = nullptr;
        if (context.has(game_content::kGameContent.name)) {
            RAWFRAME_TRY_ASSIGN(content, context.capability(game_content::kGameContent));
        }
        // From the files the host holds when it holds some (D167): the
        // description's directory's files.
        if (const composition::HeldFiles* held = context.heldFiles()) {
            const std::string_view kHeld = *kPath;
            const std::size_t kSlash = kHeld.rfind('/');
            const std::string_view kDirectory = kSlash == std::string_view::npos ? "" : kHeld.substr(0, kSlash);
            std::vector<std::pair<std::string, std::string>> files;
            for (auto& [path, bytes] : held->under(kDirectory)) {
                files.emplace_back(std::move(path),
                                   std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            }
            const std::string_view kName = kSlash == std::string_view::npos ? kHeld : kHeld.substr(kSlash + 1);
            RAWFRAME_TRY_ASSIGN(participant->files, GameFiles::fromHeld(kName, std::move(files), content));
        } else {
#if RAWFRAME_FILE_SYSTEM
            RAWFRAME_TRY_ASSIGN(participant->files, GameFiles::fromDirectory(std::string{*kPath}, content));
#else
            return result::fail(result::ErrorClass::FailedPrecondition,
                                kWorldKestDomain,
                                code(WorldKestError::UnreadableFile),
                                "kest.game names a file, and there are none here; name kest.game_resource or hold it");
#endif
        }
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
