// The cook tool (ADR-0024): cooks every sidecar-identified source under a
// directory into content-addressed artifacts, a content manifest, and a
// receipt. Import tooling, run by an author or by CI; never part of a client
// or a server.
//
//   rawframe-cook <sources> <output> [<cache>]

#include "rawframe/cook/animation.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/game.h"
#include "rawframe/cook/kest.h"
#include "rawframe/cook/mesh.h"
#include "rawframe/cook/scene.h"
#include "rawframe/cook/text.h"

#include <array>
#include <cstdio>
#include <string_view>

namespace {

void print(const rawframe::result::Error& error) {
    std::string_view where;
    for (const auto& field : error.context()) {
        if (field.key == "path") {
            where = field.value;
        }
    }
    std::fprintf(stderr,
                 "rawframe-cook: %.*s: %.*s\n",
                 static_cast<int>(where.size()),
                 where.data(),
                 static_cast<int>(error.description().size()),
                 error.description().data());
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::fputs("usage: rawframe-cook <sources> <output> [<cache>]\n", stderr);
        return 2;
    }
    // The tool's own bytes are its identity in every cook key.
    const auto kToolchain = rawframe::cook::digestOfFile("/proc/self/exe");
    if (!kToolchain.has_value()) {
        std::fputs("rawframe-cook: cannot read its own executable\n", stderr);
        return 1;
    }
    const std::array<rawframe::cook::Importer, 7> kImporters = {rawframe::cook::animationImporter(),
                                                                rawframe::cook::audioImporter(),
                                                                rawframe::cook::gameImporter(),
                                                                rawframe::cook::kestImporter(),
                                                                rawframe::cook::meshImporter(),
                                                                rawframe::cook::sceneImporter(),
                                                                rawframe::cook::textImporter()};
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
