// The cook tool (ADR-0024): cooks every sidecar-identified source under a
// directory into content-addressed artifacts, a content manifest, and a
// receipt; or, asked to map, gives each subasset a source holds and its
// sidecar does not map a new random identity, and writes the sidecar (D316).
// Import tooling, run by an author or by CI; never part of a client or a
// server.
//
//   rawframe-cook <sources> <output> [<cache>]
//   rawframe-cook --map <sources>
//
// A material whose graph the blob cannot fold is built with the shader
// toolchain (D484): the engine's tools/gen_shaders.py run by the python3 on
// the path, with the pinned Slang compiler RAWFRAME_SLANGC names or the
// slangc on the path, and dxc and spirv-cross on the path. Without them
// such a material is refused, and every other source cooks as before.

#include "rawframe/base/sha256.h"
#include "rawframe/cook/animation.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/font.h"
#include "rawframe/cook/game.h"
#include "rawframe/cook/kest.h"
#include "rawframe/cook/material.h"
#include "rawframe/cook/mesh.h"
#include "rawframe/cook/mod.h"
#include "rawframe/cook/scene.h"
#include "rawframe/cook/text.h"
#include "rawframe/cook/texture.h"
#include "rawframe/process/self.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print(const rawframe::result::Error& error) {
    std::string_view where;
    std::string_view subasset;
    for (const auto& field : error.context()) {
        if (field.key == "path") {
            where = field.value;
        } else if (field.key == "subasset") {
            subasset = field.value;
        }
    }
    std::fprintf(stderr,
                 "rawframe-cook: %.*s: %.*s%s%.*s\n",
                 static_cast<int>(where.size()),
                 where.data(),
                 static_cast<int>(error.description().size()),
                 error.description().data(),
                 subasset.empty() ? "" : ": ",
                 static_cast<int>(subasset.size()),
                 subasset.data());
}

/// A new random identity, its first half never nought: a game knows a
/// subasset by it (D314).
rawframe::content::ResourceId freshIdentity() {
    static std::random_device device;
    const auto kWord = [] {
        return (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
    };
    rawframe::base::Bits128 made{.high = kWord(), .low = kWord()};
    while (made.high == 0) {
        made.high = kWord();
    }
    return rawframe::content::ResourceId{made};
}

/// The file named `name` in the first directory of the path that has one;
/// an empty path for none.
std::filesystem::path onPath(std::string_view name) {
    const char* kPath = std::getenv("PATH");
    std::string_view path = kPath != nullptr ? kPath : "";
    while (!path.empty()) {
        const std::size_t kEnd = std::min(path.find(':'), path.size());
        const std::filesystem::path kFound = std::filesystem::path{std::string{path.substr(0, kEnd)}} / name;
        if (std::filesystem::is_regular_file(kFound)) {
            return kFound;
        }
        path.remove_prefix(std::min(kEnd + 1, path.size()));
    }
    return {};
}

/// The shader toolchain where all of it is found (D484), its identity the
/// digest of every file it runs or reads: the generator, Maul RHI's tools,
/// the scene's Slang sources, the Slang compiler with its libraries, dxc,
/// and spirv-cross; none where any is missing.
std::optional<rawframe::cook::ShaderTools> shaderTools() {
    namespace fs = std::filesystem;
    const fs::path kGenerator = RAWFRAME_SHADER_GENERATOR;
    const char* kNamed = std::getenv("RAWFRAME_SLANGC");
    const fs::path kSlangc = kNamed != nullptr ? fs::path{kNamed} : onPath("slangc");
    const fs::path kPython = onPath("python3");
    const fs::path kDxc = onPath("dxc");
    const fs::path kCross = onPath("spirv-cross");
    if (!fs::is_regular_file(kGenerator) || !fs::is_regular_file(kSlangc) || kPython.empty() || kDxc.empty() ||
        kCross.empty()) {
        return std::nullopt;
    }
    const fs::path kRoot = kGenerator.parent_path().parent_path();
    std::vector<fs::path> files = {kGenerator, kSlangc, kDxc, kCross};
    std::error_code failed;
    for (const fs::path& kDirectory : {kRoot / "third_party" / "maul-rhi" / "tools",
                                       kRoot / "modules" / "render_scene_gpu" / "shaders",
                                       kSlangc.parent_path().parent_path() / "lib"}) {
        std::vector<fs::path> held;
        for (const fs::directory_entry& each : fs::directory_iterator{kDirectory, failed}) {
            if (each.is_regular_file()) {
                held.push_back(each.path());
            }
        }
        std::ranges::sort(held);
        files.insert(files.end(), held.begin(), held.end());
    }
    rawframe::base::Sha256 identity;
    for (const fs::path& kFile : files) {
        const auto kDigest = rawframe::cook::digestOfFile(kFile);
        if (!kDigest.has_value()) {
            return std::nullopt;
        }
        identity.update(kFile.filename().string());
        identity.update(*kDigest);
    }
    return rawframe::cook::ShaderTools{.python = kPython, .generator = kGenerator, .identity = identity.finish()};
}

} // namespace

int main(int argc, char** argv) {
    const bool kMap = argc == 3 && std::string_view{argv[1]} == "--map";
    if (!kMap && argc != 3 && argc != 4) {
        std::fputs("usage: rawframe-cook <sources> <output> [<cache>]\n       rawframe-cook --map <sources>\n", stderr);
        return 2;
    }
    // The tool's own bytes are its identity in every cook key.
    const auto kToolchain = rawframe::cook::digestOfFile(rawframe::process::ownExecutable());
    if (!kToolchain.has_value()) {
        std::fputs("rawframe-cook: cannot read its own executable\n", stderr);
        return 1;
    }
    const std::array<rawframe::cook::Importer, 13> kImporters = {rawframe::cook::animationImporter(),
                                                                 rawframe::cook::audioImporter(),
                                                                 rawframe::cook::canvasImporter(),
                                                                 rawframe::cook::fontImporter(),
                                                                 rawframe::cook::gameImporter(),
                                                                 rawframe::cook::kestImporter(),
                                                                 rawframe::cook::materialImporter(shaderTools()),
                                                                 rawframe::cook::meshImporter(),
                                                                 rawframe::cook::modImporter(),
                                                                 rawframe::cook::postProcessImporter(),
                                                                 rawframe::cook::sceneImporter(),
                                                                 rawframe::cook::textImporter(),
                                                                 rawframe::cook::textureImporter()};
    if (kMap) {
        const auto kReport = rawframe::cook::mapSubassets(argv[2], kImporters, &freshIdentity);
        if (!kReport.has_value()) {
            print(kReport.error());
            return 2;
        }
        for (const auto& [kSidecar, kKeys] : kReport->written) {
            for (const std::string& key : kKeys) {
                std::printf("%s: %s\n", kSidecar.c_str(), key.c_str());
            }
        }
        for (const auto& failure : kReport->failures) {
            print(failure);
        }
        std::printf("mapped %zu sidecars, failed %zu\n", kReport->written.size(), kReport->failures.size());
        return kReport->failures.empty() ? 0 : 1;
    }
    rawframe::cook::CookRequest request{.sources = argv[1],
                                        .output = argv[2],
                                        .cache = std::nullopt,
                                        .importers = kImporters,
                                        .toolchain = *kToolchain,
                                        .target = "any"};
    if (argc == 4) {
        request.cache = argv[3];
    }
    const auto kReport = rawframe::cook::cookSources(request);
    if (!kReport.has_value()) {
        print(kReport.error());
        return 2;
    }
    for (const auto& failure : kReport->failures) {
        print(failure);
    }
    std::printf("cooked %zu, reused %zu, failed %zu\n", kReport->cooked, kReport->reused, kReport->failures.size());
    return kReport->failures.empty() ? 0 : 1;
}
