// The build tool (ADR-0024, SPEC-0021, SPEC-0023): packs a cook's
// receipt-proven output into a Build named by its root hash, signed with the
// publisher's key, and makes such keys. Packaging tooling, run by an author
// or by CI; never part of a client or a server. The engine version is this
// tool's own.
//
//   rawframe-build <cooked> <output> <subject> <version> <platform>
//                  <architecture> <side> <configuration> <profile> [<key>]
//   rawframe-build key <publisher> <directory>
//   rawframe-build install <build> <library>
//   rawframe-build compose <library> <game root> <profile> <record> [<package root>...]
//
// `key` writes `<kid>.key`, the secret, readable by its owner only, and
// `<publisher>.keys`, the publisher key set that readers pin. `install`
// copies a Build into a library as `builds/<root>/`; `compose` writes the
// CompositionRecord of the library's Build of that root as the Game, with
// the library's Builds of any package roots as its Packages, and prints its
// CompositionId.

#include "rawframe/build/build.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/document/json.h"
#include "rawframe/signature/signature.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

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

std::int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int makeKey(std::string_view publisher, const std::filesystem::path& directory) {
    const auto kKey = rawframe::build::generatePublisherKey(publisher);
    const std::int64_t kNow = unixNow();
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

std::string readText(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

/// A Build's manifest's identity section, or none: what `install` and
/// `compose` read of a Build.
std::optional<rawframe::document::Value> identityOf(const std::filesystem::path& build) {
    auto manifest = rawframe::document::parseCanonicalRecord(
        readText(build / "build.manifest"), rawframe::document::ReadLimits{.maximumBytes = 64U << 20U});
    if (!manifest.has_value() || manifest->find("identity") == nullptr) {
        return std::nullopt;
    }
    return *manifest->find("identity");
}

int install(const std::filesystem::path& build, const std::filesystem::path& library) {
    const auto kIdentity = identityOf(build);
    const auto kBytes = kIdentity.has_value() ? rawframe::document::writeCanonicalRecord(*kIdentity)
                                              : rawframe::result::Result<std::string>{std::string{}};
    if (!kIdentity.has_value() || !kBytes.has_value()) {
        std::fputs("rawframe-build: install: not a Build\n", stderr);
        return 1;
    }
    const rawframe::content::ContentDigest kRoot{.bytes = rawframe::base::sha256(*kBytes)};
    const std::filesystem::path kInto = library / "builds" / kRoot.text().substr(7);
    std::error_code error;
    std::filesystem::create_directories(kInto, error);
    std::filesystem::copy(build,
                          kInto,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                          error);
    if (error) {
        std::fputs("rawframe-build: install: the Build cannot be copied\n", stderr);
        return 1;
    }
    std::printf("installed %s\n", kRoot.text().c_str());
    return 0;
}

/// The library's Build of `root`, as a Composition names it, or none.
std::optional<rawframe::content::BuildReference> referenceTo(const std::filesystem::path& library,
                                                             std::string_view root) {
    const auto kRoot = rawframe::content::ContentDigest::parse(root);
    const auto kIdentity = kRoot.has_value() ? identityOf(library / "builds" / kRoot->text().substr(7)) : std::nullopt;
    const rawframe::document::Value* subject = kIdentity.has_value() ? kIdentity->find("subject") : nullptr;
    const rawframe::document::Value* version = kIdentity.has_value() ? kIdentity->find("version") : nullptr;
    if (subject == nullptr || version == nullptr || subject->text() == nullptr || version->text() == nullptr) {
        return std::nullopt;
    }
    return rawframe::content::BuildReference{
        .subject = *subject->text(), .version = *version->text(), .build = kRoot->bytes};
}

int compose(const std::filesystem::path& library,
            std::string_view root,
            std::string_view profile,
            const std::filesystem::path& record,
            std::span<char* const> packageRoots) {
    const auto kGame = referenceTo(library, root);
    std::vector<rawframe::content::BuildReference> packages;
    for (const char* const kPackage : packageRoots) {
        auto reference = referenceTo(library, kPackage);
        if (!reference.has_value()) {
            std::fputs("rawframe-build: compose: the library has no such Build\n", stderr);
            return 1;
        }
        packages.push_back(std::move(*reference));
    }
    if (!kGame.has_value()) {
        std::fputs("rawframe-build: compose: the library has no such Build\n", stderr);
        return 1;
    }
    // A record lists its Packages in subject order (SPEC-0021).
    std::ranges::sort(packages, {}, &rawframe::content::BuildReference::subject);
    const auto kText =
        rawframe::content::writeComposition(rawframe::content::CompositionRecord{.game = *kGame,
                                                                                 .mods = {},
                                                                                 .packages = std::move(packages),
                                                                                 .profile = std::string{profile},
                                                                                 .createdAt = unixNow()});
    if (!kText.has_value()) {
        print(kText.error());
        return 1;
    }
    std::ofstream{record, std::ios::binary} << *kText;
    const rawframe::content::ContentDigest kId{.bytes = rawframe::content::compositionIdOf(*kText)};
    std::printf("composition %s\n", kId.text().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view{argv[1]} == "key") {
        return makeKey(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "install") {
        return install(argv[2], argv[3]);
    }
    if (argc >= 6 && std::string_view{argv[1]} == "compose") {
        return compose(argv[2], argv[3], argv[4], argv[5], std::span{argv + 6, argv + argc});
    }
    if (argc != 10 && argc != 11) {
        std::fputs("usage: rawframe-build <cooked> <output> <subject> <version> <platform> <architecture> <side> "
                   "<configuration> <profile> [<key>]\n"
                   "       rawframe-build key <publisher> <directory>\n"
                   "       rawframe-build install <build> <library>\n"
                   "       rawframe-build compose <library> <game root> <profile> <record> [<package root>...]\n",
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
