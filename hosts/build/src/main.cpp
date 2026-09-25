// The build tool (ADR-0024, SPEC-0021): packs a cook's receipt-proven
// output into a Build named by its root hash. Packaging tooling, run by an
// author or by CI; never part of a client or a server. The engine version
// is this tool's own.
//
//   rawframe-build <cooked> <output> <subject> <version> <platform>
//                  <architecture> <side> <configuration> <profile>

#include "rawframe/build/build.h"

#include <cstdio>
#include <string_view>

namespace {

void print(const rawframe::result::Error& error) {
    std::string_view where;
    for (const auto& field : error.context()) {
        if (field.key == "at") {
            where = field.value;
        }
    }
    std::fprintf(stderr,
                 "rawframe-build: %.*s: %.*s\n",
                 static_cast<int>(where.size()),
                 where.data(),
                 static_cast<int>(error.description().size()),
                 error.description().data());
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 10) {
        std::fputs("usage: rawframe-build <cooked> <output> <subject> <version> <platform> <architecture> <side> "
                   "<configuration> <profile>\n",
                   stderr);
        return 2;
    }
    const rawframe::build::BuildRequest kRequest{.cooked = argv[1],
                                                 .output = argv[2],
                                                 .identity = {.subject = argv[3],
                                                              .version = argv[4],
                                                              .engine = RAWFRAME_ENGINE_VERSION,
                                                              .platform = argv[5],
                                                              .architecture = argv[6],
                                                              .side = argv[7],
                                                              .configuration = argv[8],
                                                              .profile = argv[9]}};
    const auto kReport = rawframe::build::packBuild(kRequest);
    if (!kReport.has_value()) {
        print(kReport.error());
        return 1;
    }
    const rawframe::content::ContentDigest kRoot{.bytes = kReport->root};
    std::printf("build %s, manifest %s, %zu resources, %zu blobs written, %zu reused\n",
                kRoot.text().c_str(),
                kReport->manifest.text().c_str(),
                kReport->resources,
                kReport->blobsWritten,
                kReport->blobsReused);
    return 0;
}
