#include "rawframe/kest_library/library.h"

#include "embedded.h"
#include "rawframe/kest/errors.h"

#include <algorithm>
#include <filesystem>

namespace rawframe::kest_library {

namespace {

result::Result<std::shared_ptr<const kest::Program>> refuse(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kest::kKestDomain, code(kest::KestError::DoesNotCompile), why);
}

/// A path that names one place under the game and only that one: relative,
/// already in its normal form, and never climbing out.
bool plain(std::string_view path) {
    const std::filesystem::path kPath{path};
    return !path.empty() && kPath.is_relative() && kPath.lexically_normal().generic_string() == path &&
           *kPath.begin() != ".." && path != "kest.project";
}

} // namespace

std::vector<kest::SourceFile> files() {
    std::vector<kest::SourceFile> made;
    for (const EmbeddedFile& file : embedded()) {
        made.push_back(kest::SourceFile{.path = std::string{file.path}, .text = std::string{file.text}});
    }
    return made;
}

result::Result<std::shared_ptr<const kest::Program>> compile(std::string_view entry,
                                                             std::span<const kest::SourceFile> game,
                                                             const kest::CompileSettings& settings,
                                                             std::string* report) {
    if (!std::ranges::all_of(game, [](const kest::SourceFile& file) {
            return plain(file.path);
        })) {
        return refuse("a game's file is named by a plain path under the game");
    }
    const auto kEntry = std::ranges::find(game, entry, &kest::SourceFile::path);
    if (kEntry == game.end()) {
        return refuse("the program is not among the game's files");
    }
    std::vector<kest::SourceFile> handed;
    handed.reserve(game.size() + embedded().size() + 1);
    const auto kHand = [&handed](const kest::SourceFile& file) {
        handed.push_back(kest::SourceFile{.path = std::string{kGame} + file.path, .text = file.text});
    };
    kHand(*kEntry);
    for (const kest::SourceFile& file : game) {
        if (&file != &*kEntry) {
            kHand(file);
        }
    }
    // Where on disk a project would say `source .` and name the engine's
    // modules, relative to itself.
    handed.push_back(kest::SourceFile{.path = std::string{kGame} + "kest.project",
                                      .text = "project game\nsource .\nsource ../" +
                                              std::string{kEngineModules.substr(0, kEngineModules.size() - 1)} + "\n"});
    std::ranges::move(files(), std::back_inserter(handed));
    kest::CompileSettings with = settings;
    with.library = std::string{kStandardLibrary};
    return kest::Program::compile(handed, with, report);
}

} // namespace rawframe::kest_library
