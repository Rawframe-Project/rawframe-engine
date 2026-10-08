#include "importing.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/process/child.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#include <utility>
#include <vector>

namespace rawframe::authoring_session {

namespace {

using document::Value;

/// The most bytes of a glTF's JSON read to find what it names.
constexpr std::size_t kMostGltfBytes = std::size_t{64} * 1024 * 1024;

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

/// Each extension an importer takes from outside, its keyword in a game's
/// description, and its importer.
struct Taken {
    std::string_view extension;
    std::string_view kind;
    std::string_view importer;
};

constexpr std::array<Taken, 15> kTaken = {Taken{".png", "texture", "rawframe.texture"},
                                          Taken{".jpg", "texture", "rawframe.texture"},
                                          Taken{".jpeg", "texture", "rawframe.texture"},
                                          Taken{".bmp", "texture", "rawframe.texture"},
                                          Taken{".tga", "texture", "rawframe.texture"},
                                          Taken{".hdr", "texture", "rawframe.texture"},
                                          Taken{".gltf", "mesh", "rawframe.mesh"},
                                          Taken{".glb", "mesh", "rawframe.mesh"},
                                          Taken{".wav", "sound", "rawframe.audio"},
                                          Taken{".ogg", "sound", "rawframe.audio"},
                                          Taken{".opus", "sound", "rawframe.audio"},
                                          Taken{".flac", "sound", "rawframe.audio"},
                                          Taken{".mp3", "sound", "rawframe.audio"},
                                          Taken{".ttf", "font", "rawframe.font"},
                                          Taken{".otf", "font", "rawframe.font"}};

/// Whether `path` stays where it is put: relative, with no `..` and no
/// root, drive, or scheme.
bool staysUnder(std::string_view path) {
    const std::filesystem::path kPath{std::string{path}};
    if (path.empty() || kPath.is_absolute() || kPath.has_root_name() || kPath.has_root_directory() ||
        path.find(':') != std::string_view::npos || path.find('\\') != std::string_view::npos) {
        return false;
    }
    return std::ranges::none_of(kPath, [](const std::filesystem::path& part) {
        return part == ".." || part == ".";
    });
}

/// The files a glTF names beside it, its buffers' and its images', as
/// relative paths; refused when one would leave its directory.
result::Result<std::vector<std::string>> namedBy(const std::filesystem::path& gltf) {
    std::string text;
    if (std::FILE* file = std::fopen(gltf.string().c_str(), "rb")) {
        std::array<char, 65536> buffer{};
        std::size_t got = 0;
        while ((got = std::fread(buffer.data(), 1, buffer.size(), file)) > 0 && text.size() <= kMostGltfBytes) {
            text.append(buffer.data(), got);
        }
        std::fclose(file);
    }
    const auto kRead = document::parse(text, document::ReadLimits{.maximumBytes = kMostGltfBytes});
    if (!kRead.has_value() || kRead->kind() != Value::Kind::Object) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "the glTF does not read as JSON")};
    }
    std::vector<std::string> named;
    for (const char* kList : {"buffers", "images"}) {
        const Value* kEach = kRead->find(kList);
        if (kEach == nullptr || kEach->kind() != Value::Kind::Array) {
            continue;
        }
        for (const Value& kItem : kEach->items()) {
            const Value* kUri = kItem.kind() == Value::Kind::Object ? kItem.find("uri") : nullptr;
            if (kUri == nullptr || kUri->text() == nullptr || kUri->text()->starts_with("data:")) {
                continue;
            }
            // Percent-encoded names are left out of a first import: copied
            // as written, they would name another file.
            if (!staysUnder(*kUri->text()) || kUri->text()->find('%') != std::string::npos) {
                return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                               result::ErrorClass::InvalidArgument,
                                               "a glTF imported names only files under its own directory")
                                           .withContext("uri", *kUri->text())};
            }
            if (!std::ranges::contains(named, *kUri->text())) {
                named.push_back(*kUri->text());
            }
        }
    }
    return named;
}

/// The whole of a small text file; empty for none.
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

/// `id` as a game's description writes it: 16 hex digits.
std::string hexOf(std::uint64_t id) {
    std::string made(16, '0');
    for (std::size_t at = 0; at < made.size(); ++at) {
        made[at] = "0123456789abcdef"[(id >> (60 - 4 * at)) & 0xFU];
    }
    return made;
}

} // namespace

document::Value importedOf(const Imported& made) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(made.resource.value, digits);
    Value answer = Value::object();
    answer.add("kind", Value::string("authoring.imported"));
    answer.add("asset", Value::string(made.kind));
    answer.add("id", Value::string(hexOf(made.id)));
    answer.add("resource", Value::string(std::string{digits.data(), digits.size()}));
    answer.add("path", Value::string(made.path));
    answer.add("mapped", Value::boolean(made.mapped));
    return answer;
}

bool kindOf(const std::filesystem::path& file, std::string& kind, std::string& importer) {
    std::string extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char each) {
        return static_cast<char>(std::tolower(each));
    });
    for (const Taken& kEach : kTaken) {
        if (extension == kEach.extension) {
            kind = kEach.kind;
            importer = kEach.importer;
            return true;
        }
    }
    return false;
}

result::Result<Imported> importAsset(const std::filesystem::path& game,
                                     const std::filesystem::path& source,
                                     std::string_view as,
                                     const std::filesystem::path& cook) {
    Imported made;
    std::string importer;
    if (!staysUnder(as) || !kindOf(std::filesystem::path{std::string{as}}, made.kind, importer)) {
        return std::unexpected{
            failure(authoring::AuthoringError::ValidationFailed,
                    result::ErrorClass::InvalidArgument,
                    "an import goes to a path under the game's directory, named as its kind is: "
                    "png, jpg, bmp, tga, or hdr for a texture, gltf or glb for a mesh, wav, ogg, opus, flac, or "
                    "mp3 for a sound, ttf or otf for a font")
                .withContext("as", std::string{as})};
    }
    std::string sourceKind;
    std::string sourceImporter;
    std::error_code error;
    if (!kindOf(source, sourceKind, sourceImporter) || sourceKind != made.kind ||
        !std::filesystem::is_regular_file(source, error)) {
        return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "an import's source is a file of the kind it is imported as")
                                   .withContext("source", source.string())};
    }
    const std::filesystem::path kDirectory = game.parent_path();
    const std::filesystem::path kTarget = kDirectory / std::string{as};
    const std::filesystem::path kSidecar = kTarget.string() + std::string{content::kSidecarSuffix};
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies;
    if (made.kind == "mesh" && source.extension() != ".glb") {
        RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kNamed, namedBy(source));
        for (const std::string& kEach : kNamed) {
            copies.emplace_back(source.parent_path() / kEach, kTarget.parent_path() / kEach);
        }
    }
    copies.emplace_back(source, kTarget);
    // Nothing made over anything: every target free before any is made.
    for (const std::filesystem::path& kFree : {kTarget, kSidecar}) {
        if (std::filesystem::exists(std::filesystem::symlink_status(kFree, error))) {
            return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                           result::ErrorClass::AlreadyExists,
                                           "an import's path and its sidecar's are free")
                                       .withContext("as", std::string{as})};
        }
    }
    std::vector<std::filesystem::path> madeFiles;
    const auto kUndo = [&madeFiles] {
        std::error_code ignored;
        for (const std::filesystem::path& kEach : madeFiles) {
            std::filesystem::remove(kEach, ignored);
        }
    };
    for (const auto& [kFrom, kTo] : copies) {
        // A buffer two meshes share is already there; it is the same file
        // only if the import put it there, so any other is refused.
        std::filesystem::create_directories(kTo.parent_path(), error);
        if (!std::filesystem::copy_file(kFrom, kTo, std::filesystem::copy_options::none, error)) {
            kUndo();
            return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                           result::ErrorClass::AlreadyExists,
                                           "an imported file cannot be copied where nothing is")
                                       .withContext("file", kTo.string())};
        }
        madeFiles.push_back(kTo);
    }
    std::random_device device;
    const auto kWord = [&device] {
        return (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
    };
    made.resource = content::ResourceId{.value = base::Bits128{.high = kWord() | 1U, .low = kWord()}};
    const std::string kDescription = textOf(game);
    do {
        made.id = kWord();
    } while (made.id == 0 || kDescription.find(hexOf(made.id)) != std::string::npos);
    const std::string kSidecarText =
        content::writeSidecar(content::Sidecar{.id = made.resource, .importer = std::string{importer}});
    std::FILE* sidecar = std::fopen(kSidecar.string().c_str(), "wbx");
    const bool kWritten =
        sidecar != nullptr && std::fwrite(kSidecarText.data(), 1, kSidecarText.size(), sidecar) == kSidecarText.size();
    if (sidecar == nullptr || std::fclose(sidecar) != 0 || !kWritten) {
        kUndo();
        return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                       result::ErrorClass::Unavailable,
                                       "the import's sidecar could not be written")
                                   .withContext("as", std::string{as})};
    }
    madeFiles.push_back(kSidecar);
    made.path = std::filesystem::path{std::string{as}}.generic_string();
    // The game declares it last, so a line never names what is not there.
    const std::string kLine = std::string{kDescription.empty() || kDescription.ends_with('\n') ? "" : "\n"} +
                              made.kind + " " + hexOf(made.id) + " " + made.path + "\n";
    std::FILE* description = std::fopen(game.string().c_str(), "ab");
    const bool kAdded =
        description != nullptr && std::fwrite(kLine.data(), 1, kLine.size(), description) == kLine.size();
    if (description == nullptr || std::fclose(description) != 0 || !kAdded) {
        kUndo();
        return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                       result::ErrorClass::Unavailable,
                                       "the game's description could not be added to")};
    }
    // A mesh's materials mapped to identities of their own by the cook
    // tool, as an author's `rawframe-cook --map` would (D316).
    if (made.kind == "mesh" && !cook.empty()) {
        auto mapping = process::Child::start({.program = cook, .arguments = {"--map", kDirectory.string()}});
        if (mapping.has_value()) {
            const auto kUntil = std::chrono::steady_clock::now() + std::chrono::seconds{30};
            while (!mapping->exited().has_value() && std::chrono::steady_clock::now() < kUntil) {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }
            made.mapped = mapping->exited() == 0;
            mapping->kill();
        }
    }
    return made;
}

} // namespace rawframe::authoring_session
