// The build tool (ADR-0024, SPEC-0021, SPEC-0023): packs a cook's
// receipt-proven output into a Build named by its root hash, signed with the
// publisher's key, and makes such keys. Packaging tooling, run by an author
// or by CI; never part of a client or a server. The engine version is this
// tool's own.
//
//   rawframe-build <cooked> <output> <subject> <version> <platform>
//                  <architecture> <side> <configuration> <profile> [<key>]
//   rawframe-build key <publisher> <directory>
//
// `key` writes `<kid>.key`, the secret, readable by its owner only, and
// `<publisher>.keys`, the publisher key set that readers pin.

#include "rawframe/build/build.h"
#include "rawframe/signature/signature.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

int makeKey(std::string_view publisher, const std::filesystem::path& directory) {
    const auto kKey = rawframe::build::generatePublisherKey(publisher);
    const std::int64_t kNow =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto kKeys = kKey.has_value() ? rawframe::build::keySetOf(*kKey, kNow)
                                        : rawframe::result::Result<rawframe::signature::PublisherKeySet>{
                                              std::unexpected<rawframe::result::Error>{kKey.error().clone()}};
    const auto kKeysText =
        kKeys.has_value()
            ? rawframe::signature::writePublisherKeySet(*kKeys)
            : rawframe::result::Result<std::string>{std::unexpected<rawframe::result::Error>{kKeys.error().clone()}};
    if (!kKeysText.has_value()) {
        print(kKeysText.error());
        return 1;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path kSecret = directory / (kKey->kid + ".key");
    {
        std::ofstream file{kSecret, std::ios::binary | std::ios::trunc};
        std::filesystem::permissions(kSecret,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     error);
        file << rawframe::build::writePublisherKey(*kKey);
    }
    std::ofstream{directory / (std::string{publisher} + ".keys"), std::ios::binary} << *kKeysText;
    std::printf("key %s of %.*s\n", kKey->kid.c_str(), static_cast<int>(publisher.size()), publisher.data());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view{argv[1]} == "key") {
        return makeKey(argv[2], argv[3]);
    }
    if (argc != 10 && argc != 11) {
        std::fputs("usage: rawframe-build <cooked> <output> <subject> <version> <platform> <architecture> <side> "
                   "<configuration> <profile> [<key>]\n"
                   "       rawframe-build key <publisher> <directory>\n",
                   stderr);
        return 2;
    }
    std::optional<rawframe::build::PublisherKey> signer;
    if (argc == 11) {
        std::ifstream file{argv[10], std::ios::binary};
        const std::string kText{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        auto key = rawframe::build::readPublisherKey(kText);
        if (!key.has_value()) {
            print(key.error());
            return 1;
        }
        signer = std::move(*key);
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
                                                              .profile = argv[9]},
                                                 .signer = signer.has_value() ? &*signer : nullptr};
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
