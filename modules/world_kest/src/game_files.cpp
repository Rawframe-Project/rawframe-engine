#include "rawframe/world_kest/game_files.h"

#include "game_files_participant.h"
#include "rawframe/composition/composition.h"
#include "rawframe/kest_library/library.h"
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

} // namespace

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
    RAWFRAME_TRY_ASSIGN(game.files_, kestFilesUnder(kDirectory));
    game.directory_ = kDirectory;

    base::Sha256 digest;
    field(digest, "rawframe.world_kest.game_files.v1");
    field(digest, game.text_);
    for (const Named& document : game.documents_) {
        field(digest, document.name);
        field(digest, document.text);
    }
    for (const kest::SourceFile& file : game.files_) {
        field(digest, file.path);
        field(digest, file.text);
    }
    game.digest_ = digest.finish();
    return game;
}

result::Result<std::string_view> GameFiles::document(std::string_view name) const {
    const auto kFound = std::ranges::find(documents_, name, &Named::name);
    if (kFound == documents_.end()) {
        return unreadable("the description names no such document", name);
    }
    return std::string_view{kFound->text};
}

result::Result<std::shared_ptr<const kest::Program>>
GameFiles::compile(std::string_view name, const kest::CompileSettings& settings, std::string* report) const {
    const bool kNamed =
        name == description_.program || (description_.controls && name == description_.controls->program);
    if (!kNamed) {
        return unreadable("the description names no such program", name);
    }
    if (directory_) {
        RAWFRAME_TRY_ASSIGN(const std::vector<kest::SourceFile> kNow, kestFilesUnder(*directory_));
        return kest_library::compile(name, kNow, settings, report);
    }
    return kest_library::compile(name, files_, settings, report);
}

namespace {

constexpr std::string_view kProvides[] = {kGameFiles.name};

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
    if (const auto kPath = context.configuration().text("kest.game")) {
        RAWFRAME_TRY_ASSIGN(participant->files, GameFiles::fromDirectory(std::string{*kPath}));
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
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "world_kest.game_files",
        .budgetOwner = "world",
    });
}

} // namespace rawframe::world_kest
