#include "rawframe/authoring_session/new_game.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/base/bits128.h"
#include "rawframe/content/sidecar.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace rawframe::authoring_session {

namespace {

/// A template is a few dozen files; one past these is not a template.
constexpr std::size_t kMostFiles = 4096;
constexpr std::uintmax_t kMostBytes = std::uintmax_t{256} * 1024 * 1024;

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

std::string hexOf(const base::Bits128& value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return {digits.begin(), digits.end()};
}

/// A component's identity as a description writes it: 8-4-4-4-12 digits.
std::string uuidOf(const base::Bits128& value) {
    const std::string kHex = hexOf(value);
    return kHex.substr(0, 8) + "-" + kHex.substr(8, 4) + "-" + kHex.substr(12, 4) + "-" + kHex.substr(16, 4) + "-" +
           kHex.substr(20);
}

/// Whether `text` is a component identity as a description writes it.
bool isUuid(std::string_view text) {
    if (text.size() != 36) {
        return false;
    }
    for (std::size_t at = 0; at < text.size(); ++at) {
        const bool kDash = at == 8 || at == 13 || at == 18 || at == 23;
        const char kChar = text[at];
        if (kDash ? kChar != '-' : !((kChar >= '0' && kChar <= '9') || (kChar >= 'a' && kChar <= 'f'))) {
            return false;
        }
    }
    return true;
}

/// Every occurrence of each identity in `text` made its fresh one.
std::string remapped(std::string text, const std::map<std::string, std::string, std::less<>>& fresh) {
    for (const auto& [kOld, kNew] : fresh) {
        for (std::size_t at = text.find(kOld); at != std::string::npos; at = text.find(kOld, at + kNew.size())) {
            text.replace(at, kOld.size(), kNew);
        }
    }
    return text;
}

bool writeNew(const std::filesystem::path& path, std::string_view bytes) {
    std::FILE* file = std::fopen(path.string().c_str(), "wbx");
    if (file == nullptr) {
        return false;
    }
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && kWritten;
}

} // namespace

bool isGameName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64 || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    return std::ranges::all_of(name, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
    });
}

result::Result<NewGame> newGame(const std::filesystem::path& from, const std::filesystem::path& directory) {
    using authoring::AuthoringError;
    const std::string kName = directory.filename().string();
    if (!isGameName(kName)) {
        return std::unexpected{failure(AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "a game's directory is named as its Kest project: a lowercase letter, then "
                                       "lowercase letters, digits, and _")
                                   .withContext("name", kName)};
    }
    std::error_code error;
    // Never inside the template, which would copy itself.
    const std::filesystem::path kWay = std::filesystem::weakly_canonical(directory, error)
                                           .lexically_relative(std::filesystem::weakly_canonical(from, error));
    if (!kWay.empty() && *kWay.begin() != "..") {
        return std::unexpected{failure(AuthoringError::Conflict,
                                       result::ErrorClass::InvalidArgument,
                                       "a new game goes outside its template")
                                   .withContext("directory", directory.string())};
    }
    if (std::filesystem::exists(directory, error) && !std::filesystem::is_empty(directory, error)) {
        return std::unexpected{
            failure(AuthoringError::Conflict, result::ErrorClass::AlreadyExists, "a new game goes where nothing is")
                .withContext("directory", directory.string())};
    }
    // The template's files, regular ones only, and its one description.
    std::vector<std::filesystem::path> files;
    std::uintmax_t bytes = 0;
    std::string templateName;
    for (auto entry = std::filesystem::recursive_directory_iterator(from, error);
         !error && entry != std::filesystem::recursive_directory_iterator();
         entry.increment(error)) {
        if (entry->is_symlink() || (!entry->is_regular_file() && !entry->is_directory())) {
            return std::unexpected{failure(AuthoringError::ValidationFailed,
                                           result::ErrorClass::InvalidArgument,
                                           "a template holds files and directories only")
                                       .withContext("file", entry->path().string())};
        }
        if (!entry->is_regular_file()) {
            continue;
        }
        const std::filesystem::path kRelative = entry->path().lexically_relative(from);
        files.push_back(kRelative);
        bytes += entry->file_size(error);
        if (kRelative.parent_path().empty() && kRelative.extension() == ".game") {
            if (!templateName.empty()) {
                return std::unexpected{failure(AuthoringError::ValidationFailed,
                                               result::ErrorClass::InvalidArgument,
                                               "a template holds one game description")
                                           .withContext("template", from.string())};
            }
            templateName = kRelative.stem().string();
        }
        if (files.size() > kMostFiles || bytes > kMostBytes) {
            return std::unexpected{failure(AuthoringError::LimitExceeded,
                                           result::ErrorClass::ResourceExhausted,
                                           "a template is at most 4,096 files and 256 MiB")
                                       .withContext("template", from.string())};
        }
    }
    if (error || templateName.empty()) {
        return std::unexpected{
            failure(AuthoringError::TargetNotFound, result::ErrorClass::NotFound, "a template holds a game description")
                .withContext("template", from.string())};
    }
    std::ranges::sort(files);
    // Every identity the template mints, each made fresh: its sidecars'
    // resources and subassets, and its description's components.
    std::random_device device;
    const auto kWord = [&device] {
        return (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
    };
    const auto kFresh = [&kWord] {
        return base::Bits128{.high = kWord() | 1U, .low = kWord()};
    };
    std::map<std::string, std::string, std::less<>> fresh;
    std::map<std::filesystem::path, std::string> texts;
    for (const std::filesystem::path& file : files) {
        std::optional<std::string> text = readFile(from / file);
        if (!text.has_value()) {
            return std::unexpected{
                failure(AuthoringError::TargetNotFound, result::ErrorClass::Unavailable, "a template's file reads")
                    .withContext("file", file.generic_string())};
        }
        if (file.extension().string() == content::kSidecarSuffix) {
            auto sidecar = content::readSidecar(*text);
            if (!sidecar.has_value()) {
                return std::unexpected{std::move(sidecar.error()).withContext("file", file.generic_string())};
            }
            fresh.try_emplace(hexOf(sidecar->id.value), hexOf(kFresh()));
            for (const auto& [kKey, kResource] : sidecar->subassets) {
                fresh.try_emplace(hexOf(kResource.value), hexOf(kFresh()));
            }
        }
        if (file == std::filesystem::path{templateName + ".game"}) {
            for (std::size_t start = 0; start < text->size();) {
                const std::size_t kEnd = std::min(text->find('\n', start), text->size());
                const std::string_view kLine = std::string_view{*text}.substr(start, kEnd - start);
                if (kLine.starts_with("component ") && isUuid(kLine.substr(10, 36))) {
                    fresh.try_emplace(std::string{kLine.substr(10, 36)}, uuidOf(kFresh()));
                }
                start = kEnd + 1;
            }
        }
        texts.emplace(file, std::move(*text));
    }
    // Written where nothing was; taken away whole if any part fails.
    const bool kMade = !std::filesystem::exists(directory, error);
    const auto kUndo = [&] {
        std::error_code ignored;
        if (kMade) {
            std::filesystem::remove_all(directory, ignored);
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(directory, ignored)) {
                std::filesystem::remove_all(entry.path(), ignored);
            }
        }
    };
    std::filesystem::create_directories(directory, error);
    NewGame made{.description = directory / (kName + ".game"), .files = files.size(), .identities = fresh.size()};
    for (auto& [kFile, text] : texts) {
        // A file with a nought byte is not text; its bytes are copied as
        // they are.
        if (text.find('\0') == std::string::npos) {
            text = remapped(std::move(text), fresh);
        }
        std::filesystem::path to = kFile;
        if (kFile == std::filesystem::path{templateName + ".game"}) {
            to = kName + ".game";
        } else if (kFile == std::filesystem::path{templateName + ".game" + std::string{content::kSidecarSuffix}}) {
            to = kName + ".game" + std::string{content::kSidecarSuffix};
        } else if (kFile == "kest.project") {
            // The project is the game's name; its first line says so.
            const std::size_t kEnd = text.find('\n');
            if (text.starts_with("project ")) {
                text.replace(0, std::min(kEnd, text.size()), "project " + kName);
            }
        }
        std::filesystem::create_directories((directory / to).parent_path(), error);
        if (!writeNew(directory / to, text)) {
            kUndo();
            return std::unexpected{failure(AuthoringError::Conflict,
                                           result::ErrorClass::Unavailable,
                                           "a new game's file could not be written")
                                       .withContext("file", (directory / to).string())};
        }
    }
    return made;
}

} // namespace rawframe::authoring_session
