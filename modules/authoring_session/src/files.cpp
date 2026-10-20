#include "files.h"

#include "importing.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/base/files.h"
#include "rawframe/content/sidecar.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <span>
#include <system_error>

namespace rawframe::authoring_session {

namespace {

using document::Value;

/// The most bytes of a text file read to see whether it names a path.
constexpr std::uintmax_t kMostNamingBytes = std::uintmax_t{4} * 1024 * 1024;

/// The extensions of files whose bytes are no text a path is named in.
constexpr std::array<std::string_view, 19> kUnread = {".png",
                                                      ".jpg",
                                                      ".jpeg",
                                                      ".bmp",
                                                      ".tga",
                                                      ".hdr",
                                                      ".glb",
                                                      ".wav",
                                                      ".ogg",
                                                      ".opus",
                                                      ".flac",
                                                      ".mp3",
                                                      ".ttf",
                                                      ".otf",
                                                      ".bin",
                                                      ".ktx2",
                                                      ".rfframe",
                                                      ".rfprofile",
                                                      ".picture"};

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

std::string textOf(const std::filesystem::path& path) {
    std::string text;
    if (std::FILE* file = std::fopen(path.string().c_str(), "rb")) {
        std::array<char, 4096> buffer{};
        std::size_t got = 0;
        while ((got = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
            text.append(buffer.data(), got);
        }
        std::fclose(file);
    }
    return text;
}

bool hidden(const std::filesystem::path& relative) {
    return std::ranges::any_of(relative, [](const std::filesystem::path& part) {
        return part.native().starts_with('.');
    });
}

/// Whether `path`, a path under the game's directory, is one a file
/// operation leaves alone: the game's description, its Kest project, or
/// what a dot hides.
bool kept(const std::filesystem::path& game, std::string_view path) {
    const std::filesystem::path kPath{std::string{path}};
    return kPath == game.filename() || kPath == "kest.project" || hidden(kPath);
}

/// Whether `path` under the game's directory reaches nothing through a
/// link: no part of it that is there is one, and where it is now is under
/// the directory as the file system resolves it. A collaborator's record
/// names only paths, so a link a game holds never takes a move or a
/// removal outside it (D929).
bool unlinked(const std::filesystem::path& game, std::string_view path) {
    std::error_code error;
    const std::filesystem::path kRoot = std::filesystem::canonical(game.parent_path(), error);
    if (error) {
        return false;
    }
    std::filesystem::path at = kRoot;
    for (const std::filesystem::path& kPart : std::filesystem::path{std::string{path}}) {
        at /= kPart;
        const std::filesystem::file_status kStatus = std::filesystem::symlink_status(at, error);
        if (std::filesystem::is_symlink(kStatus)) {
            return false;
        }
        if (!std::filesystem::exists(kStatus)) {
            break;
        }
    }
    const std::filesystem::path kResolved = std::filesystem::weakly_canonical(kRoot / std::string{path}, error);
    const auto [kEnd, kAt] = std::ranges::mismatch(kRoot, kResolved);
    return !error && kEnd == kRoot.end();
}

/// Refused unless `path` is under the game's directory, reached through
/// no link, and not kept.
result::Status movable(const std::filesystem::path& game, std::string_view path) {
    if (!staysUnder(path) || kept(game, path) || !unlinked(game, path)) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "a file moved or removed is under the game's directory, reached through "
                                       "no link, and neither its description nor its Kest project")
                                   .withContext("path", std::string{path})};
    }
    return {};
}

/// Whether `token` is `path` or, for a folder, under it; what follows it
/// there, if so.
std::optional<std::string_view> under(std::string_view token, std::string_view path) {
    if (token == path) {
        return std::string_view{};
    }
    if (token.size() > path.size() && token.starts_with(path) && token[path.size()] == '/') {
        return token.substr(path.size());
    }
    return std::nullopt;
}

/// The description's lines each a token of which names `from` renamed to
/// `to`, or with `to` empty left out; how many.
std::size_t renameLines(std::string& description, std::string_view from, std::string_view to) {
    std::string made;
    std::size_t changed = 0;
    std::size_t at = 0;
    while (at < description.size()) {
        std::size_t end = description.find('\n', at);
        const bool kEnded = end != std::string::npos;
        end = kEnded ? end : description.size();
        const std::string_view kLine{description.data() + at, end - at};
        at = end + 1;
        std::string line;
        bool named = false;
        if (!kLine.starts_with('#')) {
            std::size_t tokenAt = 0;
            while (tokenAt <= kLine.size()) {
                std::size_t space = kLine.find(' ', tokenAt);
                space = space == std::string_view::npos ? kLine.size() : space;
                const std::string_view kToken = kLine.substr(tokenAt, space - tokenAt);
                if (const auto kRest = under(kToken, from); kRest.has_value()) {
                    named = true;
                    line += std::string{to} + std::string{*kRest};
                } else {
                    line += kToken;
                }
                if (space < kLine.size()) {
                    line += ' ';
                }
                tokenAt = space + 1;
            }
        } else {
            line = kLine;
        }
        changed += named ? 1 : 0;
        if (named && to.empty()) {
            continue;
        }
        made += named ? line : std::string{kLine};
        if (kEnded) {
            made += '\n';
        }
    }
    description = std::move(made);
    return changed;
}

/// Whether `text` names `path` as a whole: not as part of a longer name.
bool names(std::string_view text, std::string_view path) {
    const auto kPart = [](char each) {
        return std::isalnum(static_cast<unsigned char>(each)) != 0 || each == '_' || each == '.' || each == '-';
    };
    for (std::size_t at = text.find(path); at != std::string_view::npos; at = text.find(path, at + 1)) {
        const bool kStarts = at == 0 || (!kPart(text[at - 1]) && text[at - 1] != '/');
        const std::size_t kEnd = at + path.size();
        const bool kEnds = kEnd == text.size() || !kPart(text[kEnd]);
        if (kStarts && kEnds) {
            return true;
        }
    }
    return false;
}

/// The game's text files, but its description and sidecars, that name
/// `path`, by their paths under its directory.
std::vector<std::string> namedIn(const std::filesystem::path& game, std::string_view path) {
    std::vector<std::string> made;
    const std::filesystem::path kRoot = game.parent_path();
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator{kRoot, error};
         !error && entry != std::filesystem::recursive_directory_iterator{};
         entry.increment(error)) {
        std::error_code relative;
        const std::filesystem::path kRelative = std::filesystem::relative(entry->path(), kRoot, relative);
        if (hidden(kRelative)) {
            if (entry->is_directory()) {
                entry.disable_recursion_pending();
            }
            continue;
        }
        std::string extension = entry->path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char each) {
            return static_cast<char>(std::tolower(each));
        });
        std::error_code sized;
        if (entry->is_symlink() || !entry->is_regular_file() || entry->path() == game ||
            extension == content::kSidecarSuffix || std::ranges::contains(kUnread, extension) ||
            entry->file_size(sized) > kMostNamingBytes) {
            continue;
        }
        if (names(textOf(entry->path()), path)) {
            made.push_back(kRelative.generic_string());
        }
    }
    std::ranges::sort(made);
    return made;
}

result::Status rewritten(const std::filesystem::path& game, const std::string& text) {
    if (!base::replaceFile(game, std::as_bytes(std::span{text.data(), text.size()}))) {
        return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                       result::ErrorClass::Unavailable,
                                       "the game's description could not be written")};
    }
    return {};
}

} // namespace

document::Value filesChangedOf(const FilesChanged& changed, bool removed) {
    Value made = Value::object();
    made.add("kind", Value::string(removed ? "authoring.removed" : "authoring.moved"));
    made.add("from", Value::string(changed.from));
    if (!removed) {
        made.add("to", Value::string(changed.to));
    }
    Value files = Value::array();
    for (const std::string& kFile : changed.files) {
        files.push(Value::string(kFile));
    }
    made.add("files", std::move(files));
    made.add("lines", Value::integer(static_cast<std::int64_t>(changed.lines)));
    Value named = Value::array();
    for (const std::string& kFile : changed.named) {
        named.push(Value::string(kFile));
    }
    made.add("named", std::move(named));
    return made;
}

std::vector<std::string> foldersBeside(const std::filesystem::path& game) {
    std::vector<std::string> made;
    const std::filesystem::path kRoot = game.parent_path();
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator{kRoot, error};
         !error && entry != std::filesystem::recursive_directory_iterator{};
         entry.increment(error)) {
        if (!entry->is_directory()) {
            continue;
        }
        std::error_code relative;
        const std::filesystem::path kRelative = std::filesystem::relative(entry->path(), kRoot, relative);
        if (hidden(kRelative)) {
            entry.disable_recursion_pending();
            continue;
        }
        made.push_back(kRelative.generic_string());
    }
    std::ranges::sort(made);
    return made;
}

result::Result<FilesChanged> moveFile(const std::filesystem::path& game, std::string_view from, std::string_view to) {
    RAWFRAME_TRY(movable(game, from));
    RAWFRAME_TRY(movable(game, to));
    const std::filesystem::path kRoot = game.parent_path();
    const std::filesystem::path kFrom = kRoot / std::string{from};
    const std::filesystem::path kTo = kRoot / std::string{to};
    std::error_code error;
    const std::filesystem::file_status kStatus = std::filesystem::symlink_status(kFrom, error);
    if (!std::filesystem::is_regular_file(kStatus) && !std::filesystem::is_directory(kStatus)) {
        return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "a file moved is a file or a folder under the game's directory")
                                   .withContext("from", std::string{from})};
    }
    const bool kFolder = std::filesystem::is_directory(kStatus);
    if (kFolder && under(to, from).has_value()) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "a folder is not moved into itself")
                                   .withContext("to", std::string{to})};
    }
    // A glTF's buffers and images are beside it, named relative to it.
    if (!kFolder && kFrom.extension() == ".gltf" && kFrom.parent_path() != kTo.parent_path()) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "a glTF names its files beside it: renamed in its folder, or moved with "
                                       "the folder")
                                   .withContext("from", std::string{from})};
    }
    const std::filesystem::path kSidecar = kFrom.string() + std::string{content::kSidecarSuffix};
    const std::filesystem::path kSidecarTo = kTo.string() + std::string{content::kSidecarSuffix};
    const bool kSided = !kFolder && std::filesystem::is_regular_file(kSidecar, error);
    // Nothing moved over anything.
    for (const std::filesystem::path& kFree : {kTo, kSidecarTo}) {
        if ((kFree != kSidecarTo || kSided) && std::filesystem::exists(std::filesystem::symlink_status(kFree, error))) {
            return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                           result::ErrorClass::AlreadyExists,
                                           "a file moves only where nothing is")
                                       .withContext("to", std::string{to})};
        }
    }
    FilesChanged made{.from = std::string{from}, .to = std::string{to}};
    std::filesystem::create_directories(kTo.parent_path(), error);
    std::filesystem::rename(kFrom, kTo, error);
    if (error) {
        return std::unexpected{
            failure(authoring::AuthoringError::Conflict, result::ErrorClass::Unavailable, "the file could not be moved")
                .withContext("from", std::string{from})
                .withContext("said", error.message())};
    }
    made.files.push_back(made.from);
    if (kSided) {
        std::filesystem::rename(kSidecar, kSidecarTo, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::rename(kTo, kFrom, ignored);
            return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                           result::ErrorClass::Unavailable,
                                           "the file's sidecar could not be moved with it")
                                       .withContext("from", std::string{from})};
        }
        made.files.push_back(made.from + std::string{content::kSidecarSuffix});
    }
    std::string description = textOf(game);
    made.lines = renameLines(description, from, to);
    if (made.lines > 0) {
        RAWFRAME_TRY(rewritten(game, description));
    }
    made.named = namedIn(game, from);
    return made;
}

result::Result<FilesChanged> removeFile(const std::filesystem::path& game, std::string_view path) {
    RAWFRAME_TRY(movable(game, path));
    const std::filesystem::path kPath = game.parent_path() / std::string{path};
    std::error_code error;
    const std::filesystem::file_status kStatus = std::filesystem::symlink_status(kPath, error);
    if (!std::filesystem::is_regular_file(kStatus) && !std::filesystem::is_directory(kStatus)) {
        return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "a file removed is a file or a folder under the game's directory")
                                   .withContext("path", std::string{path})};
    }
    FilesChanged made{.from = std::string{path}};
    // The description's lines first, so it never names what is gone.
    std::string description = textOf(game);
    made.lines = renameLines(description, path, {});
    if (made.lines > 0) {
        RAWFRAME_TRY(rewritten(game, description));
    }
    if (std::filesystem::is_directory(kStatus)) {
        std::filesystem::remove_all(kPath, error);
    } else {
        std::filesystem::remove(kPath, error);
        const std::filesystem::path kSidecar = kPath.string() + std::string{content::kSidecarSuffix};
        std::error_code sided;
        if (!error && std::filesystem::remove(kSidecar, sided)) {
            made.files.push_back(made.from + std::string{content::kSidecarSuffix});
        }
    }
    if (error) {
        return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                       result::ErrorClass::Unavailable,
                                       "the file could not be removed")
                                   .withContext("path", std::string{path})
                                   .withContext("said", error.message())};
    }
    made.files.insert(made.files.begin(), made.from);
    made.named = namedIn(game, path);
    return made;
}

result::Result<document::Value> makeFolder(const std::filesystem::path& game, std::string_view folder) {
    RAWFRAME_TRY(movable(game, folder));
    const std::filesystem::path kFolder = game.parent_path() / std::string{folder};
    std::error_code error;
    if (std::filesystem::exists(std::filesystem::symlink_status(kFolder, error))) {
        return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                       result::ErrorClass::AlreadyExists,
                                       "a folder is made where nothing is")
                                   .withContext("folder", std::string{folder})};
    }
    if (!std::filesystem::create_directories(kFolder, error) || error) {
        return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                       result::ErrorClass::Unavailable,
                                       "the folder could not be made")
                                   .withContext("folder", std::string{folder})};
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.folder_made"));
    made.add("folder", Value::string(std::filesystem::path{std::string{folder}}.generic_string()));
    return made;
}

} // namespace rawframe::authoring_session
