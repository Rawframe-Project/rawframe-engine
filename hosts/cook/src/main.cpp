// The cook tool (ADR-0024): cooks every sidecar-identified source under a
// directory into content-addressed artifacts, a content manifest, and a
// receipt. Import tooling, run by an author or by CI; never part of a client
// or a server.
//
//   rawframe-cook <sources> <output> [<cache>]

#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"

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
    const std::array<rawframe::cook::Importer, 1> kImporters = {rawframe::cook::audioImporter()};
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
