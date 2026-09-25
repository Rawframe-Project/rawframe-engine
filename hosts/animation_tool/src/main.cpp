// The animation tool (ADR-0039, SPEC-0035): authoring help for animation
// documents, run by an author on their own files; never part of a client or
// a server.
//
//   rawframe-animation from-gltf <source.gltf | source.glb> <directory> [loop]
//
// `from-gltf` writes the source's first skin as `skeleton.rfanim` and each
// of its animations as `<name>.rfanim` in the directory, each beside a
// sidecar naming it for the cook's `rawframe.animation` importer (D125). A
// document whose sidecar is already there keeps its resource identity, so
// importing again replaces the documents and nothing that names them breaks;
// otherwise the identity is new. An animation's name is lowercased, and what
// is not a letter, a digit, or `-` becomes `_`. With `loop`, every clip
// repeats.

#include "rawframe/animation/clip.h"
#include "rawframe/animation/errors.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/animation_import/import.h"
#include "rawframe/content/sidecar.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace animation = rawframe::animation;
namespace fs = std::filesystem;

void print(const rawframe::result::Error& error) {
    std::fprintf(
        stderr, "rawframe-animation: %.*s\n", static_cast<int>(error.description().size()), error.description().data());
    for (const auto& field : error.context()) {
        std::fprintf(stderr,
                     "  %.*s: %.*s\n",
                     static_cast<int>(field.key.size()),
                     field.key.data(),
                     static_cast<int>(field.value.size()),
                     field.value.data());
    }
}

std::vector<std::byte> readBytes(const fs::path& path, bool& read) {
    std::ifstream file{path, std::ios::binary};
    read = static_cast<bool>(file);
    const std::vector<char> kRead{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(kRead.size());
    if (!kRead.empty()) {
        std::memcpy(bytes.data(), kRead.data(), kRead.size());
    }
    return bytes;
}

/// The identity the sidecar beside `document` gives it, or a new random one.
rawframe::base::Bits128 identityFor(const fs::path& document) {
    bool read = false;
    const std::vector<std::byte> kText = readBytes(fs::path{document.string() + ".rfmeta"}, read);
    if (read) {
        const auto kSidecar =
            rawframe::content::readSidecar(std::string_view{reinterpret_cast<const char*>(kText.data()), kText.size()});
        if (kSidecar.has_value() && kSidecar->importer == "rawframe.animation") {
            return kSidecar->id.value;
        }
    }
    std::random_device device;
    const auto kWord = [&device] {
        return (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
    };
    return rawframe::base::Bits128{.high = kWord(), .low = kWord()};
}

void writeDocument(const fs::path& document, std::string_view text, rawframe::base::Bits128 id) {
    std::array<char, rawframe::base::kBits128HexDigits> digits{};
    rawframe::base::formatBits128Hex(id, digits);
    std::ofstream{document, std::ios::binary} << text;
    std::ofstream{fs::path{document.string() + ".rfmeta"}, std::ios::binary}
        << "{\n  \"schema\": 1,\n  \"resourceId\": \"" << std::string_view{digits.data(), digits.size()}
        << "\",\n  \"importer\": \"rawframe.animation\"\n}\n";
}

std::string fileNameOf(std::string_view name) {
    std::string made;
    for (const char kLetter : name) {
        const bool kKept = (kLetter >= 'a' && kLetter <= 'z') || (kLetter >= '0' && kLetter <= '9') || kLetter == '-';
        const bool kUpper = kLetter >= 'A' && kLetter <= 'Z';
        made.push_back(kKept ? kLetter : kUpper ? static_cast<char>(kLetter - 'A' + 'a') : '_');
    }
    return made;
}

int fromGltf(const fs::path& source, const fs::path& directory, bool loop) {
    bool read = false;
    const std::vector<std::byte> kSource = readBytes(source, read);
    if (!read) {
        std::fprintf(stderr, "rawframe-animation: cannot read %s\n", source.string().c_str());
        return 1;
    }
    // Buffers the glTF names, beside it, kept until the import returns.
    std::map<std::string, std::vector<std::byte>> buffers;
    const rawframe::animation_import::ReadFile kRead =
        [&](std::string_view path) -> rawframe::result::Result<std::span<const std::byte>> {
        bool found = false;
        std::vector<std::byte> bytes = readBytes(source.parent_path() / path, found);
        if (!found) {
            return rawframe::result::fail(rawframe::result::ErrorClass::NotFound,
                                          animation::kAnimationDomain,
                                          animation::code(animation::AnimationError::BadSource),
                                          "a buffer the glTF names cannot be read");
        }
        const auto& kKept = buffers.insert_or_assign(std::string{path}, std::move(bytes)).first->second;
        return std::span<const std::byte>{kKept};
    };
    fs::create_directories(directory);
    const fs::path kSkeletonPath = directory / "skeleton.rfanim";
    const rawframe::base::Bits128 kSkeletonId = identityFor(kSkeletonPath);
    auto imported = rawframe::animation_import::importGltf(
        kSource, kRead, rawframe::animation_import::ImportSettings{.skeleton = kSkeletonId, .loop = loop});
    if (!imported.has_value()) {
        print(imported.error());
        return 1;
    }
    // Every document is written first in memory, so a refusal leaves the
    // directory as it was.
    std::vector<std::pair<fs::path, std::string>> documents;
    auto skeleton = animation::writeSkeleton(imported->skeleton);
    if (!skeleton.has_value()) {
        print(skeleton.error());
        return 1;
    }
    documents.emplace_back(kSkeletonPath, std::move(*skeleton));
    for (const auto& clip : imported->clips) {
        const fs::path kPath = directory / (fileNameOf(clip.name) + ".rfanim");
        for (const auto& [kWritten, kText] : documents) {
            if (kWritten == kPath) {
                std::fprintf(stderr, "rawframe-animation: two documents would be %s\n", kPath.string().c_str());
                return 1;
            }
        }
        auto text = animation::writeClip(clip.clip);
        if (!text.has_value()) {
            print(std::move(text).error().withContext("animation", clip.name));
            return 1;
        }
        documents.emplace_back(kPath, std::move(*text));
    }
    for (const auto& [kPath, kText] : documents) {
        writeDocument(kPath, kText, kPath == kSkeletonPath ? kSkeletonId : identityFor(kPath));
    }
    std::printf("skeleton of %zu bones, %zu clips, %zu channels skipped, %zu cubic rotations turned by slerp\n",
                imported->skeleton.bones.size(),
                imported->clips.size(),
                imported->channelsSkipped,
                imported->rotationsFlattened);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool kLoop = argc == 5 && std::string_view{argv[4]} == "loop";
    if ((argc == 4 || kLoop) && std::string_view{argv[1]} == "from-gltf") {
        return fromGltf(argv[2], argv[3], kLoop);
    }
    std::fputs("usage: rawframe-animation from-gltf <source.gltf | source.glb> <directory> [loop]\n", stderr);
    return 2;
}
