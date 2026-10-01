// The cook tool (ADR-0024): cooks every sidecar-identified source under a
// directory into content-addressed artifacts, a content manifest, and a
// receipt; or, asked to map, gives each subasset a source holds and its
// sidecar does not map a new random identity, and writes the sidecar (D316).
// Import tooling, run by an author or by CI; never part of a client or a
// server.
//
//   rawframe-cook <sources> <output> [<cache>]
//   rawframe-cook --map <sources>

#include "rawframe/cook/animation.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/game.h"
#include "rawframe/cook/kest.h"
#include "rawframe/cook/material.h"
#include "rawframe/cook/mesh.h"
#include "rawframe/cook/mod.h"
#include "rawframe/cook/scene.h"
#include "rawframe/cook/text.h"
#include "rawframe/cook/texture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#endif

namespace {

/// Where this program's own file is, to read it (D236, D237 on Windows).
std::filesystem::path ownExecutable() {
#if defined(_WIN32)
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD kLength = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (kLength == 0) {
            return {};
        }
        if (kLength < path.size()) {
            path.resize(kLength);
            return path;
        }
        path.resize(path.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    static_cast<void>(::_NSGetExecutablePath(nullptr, &size));
    std::string path(size, '\0');
    if (::_NSGetExecutablePath(path.data(), &size) != 0) {
        return {};
    }
    path.resize(path.find('\0'));
    return path;
#else
    return "/proc/self/exe";
#endif
}

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

} // namespace

int main(int argc, char** argv) {
    const bool kMap = argc == 3 && std::string_view{argv[1]} == "--map";
    if (!kMap && argc != 3 && argc != 4) {
        std::fputs("usage: rawframe-cook <sources> <output> [<cache>]\n       rawframe-cook --map <sources>\n", stderr);
        return 2;
    }
    // The tool's own bytes are its identity in every cook key.
    const auto kToolchain = rawframe::cook::digestOfFile(ownExecutable());
    if (!kToolchain.has_value()) {
        std::fputs("rawframe-cook: cannot read its own executable\n", stderr);
        return 1;
    }
    const std::array<rawframe::cook::Importer, 12> kImporters = {rawframe::cook::animationImporter(),
                                                                 rawframe::cook::audioImporter(),
                                                                 rawframe::cook::canvasImporter(),
                                                                 rawframe::cook::gameImporter(),
                                                                 rawframe::cook::kestImporter(),
                                                                 rawframe::cook::materialImporter(),
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
