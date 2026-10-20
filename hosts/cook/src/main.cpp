// The cook tool (ADR-0024): cooks every sidecar-identified source under a
// directory into content-addressed artifacts, a content manifest, and a
// receipt; or, asked to map, gives each subasset a source holds and its
// sidecar does not map a new random identity, and writes the sidecar (D316).
// Import tooling, run by an author or by CI; never part of a client or a
// server.
//
//   rawframe-cook <sources> <output> [<cache>]
//   rawframe-cook --map <sources> [<source>]
//
// Each source is told on standard output as it is begun
// (`cooking <n>/<count> <source>`), so a caller shows how far it is; a stop
// request (SIGTERM, or Ctrl+Break on Windows) stops the cook before the next
// source, publishing nothing, and it ends with 3 (SPEC-0040's long-running
// operations, D502).
//
// A material whose graph the blob cannot fold is built with the shader
// toolchain (D484): the engine's tools/gen_shaders.py run by the python3 on
// the path (`python` or `py` on Windows), with the pinned Slang compiler
// RAWFRAME_SLANGC names or the slangc on the path, and dxc, spirv-cross,
// spirv-val, spirv-dis and spirv-as on the path; or the toolchain Studio's
// package lays beside this tool in `shader-tools` (D945). Without them such
// a material is refused, and every other source cooks as before.

#include "rawframe/host/main.h"

#include "rawframe/base/sha256.h"
#include "rawframe/cook/animation.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/font.h"
#include "rawframe/cook/game.h"
#include "rawframe/cook/importers.h"
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
#include <atomic>
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

/// The program named `name` in the first directory of the path that has
/// one, with the system's ending for a program (`.exe` on Windows, whose
/// path is split at `;`, and never its WindowsApps aliases); an empty path
/// for none.
std::filesystem::path onPath(std::string_view name) {
#if defined(_WIN32)
    constexpr char kSeparator = ';';
    const std::string kFile = std::string{name} + ".exe";
#else
    constexpr char kSeparator = ':';
    const std::string kFile{name};
#endif
    const char* kPath = std::getenv("PATH");
    std::string_view path = kPath != nullptr ? kPath : "";
    while (!path.empty()) {
        const std::size_t kEnd = std::min(path.find(kSeparator), path.size());
        const std::string_view kDirectory = path.substr(0, kEnd);
        const std::filesystem::path kFound = std::filesystem::path{std::string{kDirectory}} / kFile;
        // Windows' WindowsApps holds python.exe that opens the Store.
        if (!kDirectory.ends_with("WindowsApps") && std::filesystem::is_regular_file(kFound)) {
            return kFound;
        }
        path.remove_prefix(std::min(kEnd + 1, path.size()));
    }
    return {};
}

/// The program `name` in the `bin` of a toolchain packaged beside this
/// tool (D945); an empty path for none.
std::filesystem::path packagedTool(const std::filesystem::path& packaged, std::string_view name) {
#if defined(_WIN32)
    const std::filesystem::path kBeside = packaged / "bin" / (std::string{name} + ".exe");
#else
    const std::filesystem::path kBeside = packaged / "bin" / std::string{name};
#endif
    std::error_code error;
    return std::filesystem::is_regular_file(kBeside, error) ? kBeside : std::filesystem::path{};
}

/// The shader toolchain where all of it is found (D484), its identity the
/// digest of every file it runs or reads: the generator, Maul RHI's tools
/// and the contract its container writer reads, the scene's Slang sources, the Slang compiler with its libraries, dxc,
/// spirv-cross, spirv-val, spirv-dis, and spirv-as; none where any is
/// missing. A
/// toolchain packaged beside this tool (`shader-tools`, as Studio's package
/// lays one out, D945) is taken first: the generator and the sources under
/// it as in the engine's tree, the programs in its `bin`; else the engine
/// tree's generator this tool was built from, and the programs on the path
/// (the Slang compiler where RAWFRAME_SLANGC names it, unless the package
/// has its own). Python is `python3`, or on Windows `python` or `py`.
std::optional<rawframe::cook::ShaderTools> shaderTools() {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path kPackaged = fs::canonical(rawframe::process::ownExecutable(), error).parent_path() / "shader-tools";
    const fs::path kBesideGenerator = kPackaged / "tools" / "gen_shaders.py";
    const bool kBeside = fs::is_regular_file(kBesideGenerator, error);
    const fs::path kGenerator = kBeside ? kBesideGenerator : fs::path{RAWFRAME_SHADER_GENERATOR};
    const auto kTool = [&kPackaged](std::string_view name) {
        const fs::path kFound = packagedTool(kPackaged, name);
        return kFound.empty() ? onPath(name) : kFound;
    };
    const char* kNamed = std::getenv("RAWFRAME_SLANGC");
    const fs::path kOwnSlangc = packagedTool(kPackaged, "slangc");
    const fs::path kSlangc = !kOwnSlangc.empty() ? kOwnSlangc : kNamed != nullptr ? fs::path{kNamed} : onPath("slangc");
    fs::path python = onPath("python3");
#if defined(_WIN32)
    // Python's installer for Windows puts `py` on the path by default, and
    // `python` only when asked to.
    for (const std::string_view kName : {"python", "py"}) {
        if (python.empty()) {
            python = onPath(kName);
        }
    }
#endif
    const fs::path kDxc = kTool("dxc");
    const fs::path kCross = kTool("spirv-cross");
    const fs::path kValidator = kTool("spirv-val");
    const fs::path kDisassembler = kTool("spirv-dis");
    const fs::path kAssembler = kTool("spirv-as");
    if (!fs::is_regular_file(kGenerator) || !fs::is_regular_file(kSlangc) || python.empty() || kDxc.empty() ||
        kCross.empty() || kValidator.empty() || kDisassembler.empty() || kAssembler.empty()) {
        return std::nullopt;
    }
    const fs::path kRoot = kGenerator.parent_path().parent_path();
    std::vector<fs::path> files = {kGenerator,
                                   kRoot / "third_party" / "maul-rhi" / "docs" / "contract" / "mrhi.json",
                                   kSlangc,
                                   kDxc,
                                   kCross,
                                   kValidator,
                                   kDisassembler,
                                   kAssembler};
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
    // On Windows the Slang compiler's libraries are beside it.
    std::vector<fs::path> libraries;
    for (const fs::directory_entry& each : fs::directory_iterator{kSlangc.parent_path(), failed}) {
        if (each.is_regular_file() && each.path().extension() == ".dll") {
            libraries.push_back(each.path());
        }
    }
    std::ranges::sort(libraries);
    files.insert(files.end(), libraries.begin(), libraries.end());
    rawframe::base::Sha256 identity;
    for (const fs::path& kFile : files) {
        const auto kDigest = rawframe::cook::digestOfFile(kFile);
        if (!kDigest.has_value()) {
            return std::nullopt;
        }
        identity.update(kFile.filename().string());
        identity.update(*kDigest);
    }
    return rawframe::cook::ShaderTools{.python = python, .generator = kGenerator, .identity = identity.finish()};
}

} // namespace

int main(int argc, char** argv) {
    const bool kMap = (argc == 3 || argc == 4) && std::string_view{argv[1]} == "--map";
    if (!kMap && argc != 3 && argc != 4) {
        std::fputs(
            "usage: rawframe-cook <sources> <output> [<cache>]\n       rawframe-cook --map <sources> [<source>]\n",
            stderr);
        return 2;
    }
    // The tool's own bytes are its identity in every cook key.
    const auto kToolchain = rawframe::cook::digestOfFile(rawframe::process::ownExecutable());
    if (!kToolchain.has_value()) {
        std::fputs("rawframe-cook: cannot read its own executable\n", stderr);
        return 1;
    }
    const std::vector<rawframe::cook::Importer> kImporters = rawframe::cook::engineImporters(shaderTools());
    if (kMap) {
        // One source alone where named (D536), its path under the sources.
        const auto kReport = rawframe::cook::mapSubassets(
            argv[2], kImporters, &freshIdentity, argc == 4 ? std::optional<std::string>{argv[3]} : std::nullopt);
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
    rawframe::host::installStopBridge();
    request.step = [](const rawframe::cook::CookStep& step) {
        if (rawframe::host::stopRequested().load(std::memory_order_acquire)) {
            return false;
        }
        std::printf("cooking %zu/%zu %.*s\n",
                    step.index + 1,
                    step.count,
                    static_cast<int>(step.source.size()),
                    step.source.data());
        std::fflush(stdout);
        return true;
    };
    const auto kReport = rawframe::cook::cookSources(request);
    if (!kReport.has_value()) {
        print(kReport.error());
        return 2;
    }
    for (const auto& failure : kReport->failures) {
        print(failure);
    }
    if (kReport->stopped) {
        std::printf("stopped, cooked %zu, reused %zu, published nothing\n", kReport->cooked, kReport->reused);
        return 3;
    }
    std::printf("cooked %zu, reused %zu, failed %zu\n", kReport->cooked, kReport->reused, kReport->failures.size());
    return kReport->failures.empty() ? 0 : 1;
}
