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
//   rawframe-build compose <library> <game root> <profile> <record> [<package root> | --mod <mod root>]...
//   rawframe-build release <mirror> <record> <version> <channel> <key>
//   rawframe-build point <mirror> <subject> <channel> <release> <key>
//
// `key` writes `<kid>.key`, the secret, readable by its owner only, and
// `<publisher>.keys`, the publisher key set that readers pin. `install`
// adds a Build to a library (`install::Installation`), verified against
// the key set the library pins for its publisher; `compose` writes the
// CompositionRecord of the library's Build of that root as the Game, with
// the library's Builds of any package roots as its Packages and of any mod
// roots as its Mods, and prints its CompositionId. Whether the game takes
// those mods is decided when the Composition is opened (D179).
//
// `release` publishes a CompositionRecord to a mirror as a Release of its
// game's subject (SPEC-0020, D424): the record kept by its CompositionId,
// a ReleaseRecord naming it (after the Release the channel pointed at, if
// any), and the channel pointed at it with the next sequence, each signed
// with the key. `point` points a channel at a Release the mirror holds,
// with the next sequence: an older one is a rollback. Each prints the
// Release and the sequence.

#include "rawframe/build/build.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/content/library.h"
#include "rawframe/document/json.h"
#include "rawframe/install/installation.h"
#include "rawframe/release/release.h"
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
    auto manifest =
        rawframe::document::parseCanonicalRecord(readText(build / rawframe::content::kBuildManifestName),
                                                 rawframe::document::ReadLimits{.maximumBytes = 64U << 20U});
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
    // What the Build says it is, to be verified against the key set the
    // library pins for its publisher before a blob of it is taken.
    const rawframe::document::Value* subject = kIdentity->find("subject");
    const rawframe::document::Value* version = kIdentity->find("version");
    if (subject == nullptr || version == nullptr || subject->text() == nullptr || version->text() == nullptr) {
        std::fputs("rawframe-build: install: not a Build\n", stderr);
        return 1;
    }
    auto installation = rawframe::install::Installation::open(library);
    if (!installation.has_value()) {
        print(installation.error());
        return 1;
    }
    auto origin = rawframe::install::packedBuildAt(build);
    if (!origin.has_value()) {
        print(origin.error());
        return 1;
    }
    const auto kAdded =
        installation->add({.subject = *subject->text(), .version = *version->text(), .build = kRoot.bytes}, **origin);
    if (!kAdded.has_value()) {
        print(kAdded.error());
        return 1;
    }
    std::printf("installed %s\n", kRoot.text().c_str());
    return 0;
}

/// The library's Build of `root`, as a Composition names it, or none.
std::optional<rawframe::content::BuildReference> referenceTo(const std::filesystem::path& library,
                                                             std::string_view root) {
    const auto kRoot = rawframe::content::ContentDigest::parse(root);
    const auto kIdentity =
        kRoot.has_value() ? identityOf(library / rawframe::content::buildDirectoryOf(kRoot->bytes)) : std::nullopt;
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
            std::span<char* const> roots) {
    const auto kGame = referenceTo(library, root);
    std::vector<rawframe::content::BuildReference> packages;
    std::vector<rawframe::content::BuildReference> mods;
    for (std::size_t at = 0; at < roots.size(); ++at) {
        const bool kMod = std::string_view{roots[at]} == "--mod" && at + 1 < roots.size();
        auto reference = referenceTo(library, roots[kMod ? ++at : at]);
        if (!reference.has_value()) {
            std::fputs("rawframe-build: compose: the library has no such Build\n", stderr);
            return 1;
        }
        (kMod ? mods : packages).push_back(std::move(*reference));
    }
    if (!kGame.has_value()) {
        std::fputs("rawframe-build: compose: the library has no such Build\n", stderr);
        return 1;
    }
    // A record lists its Packages and Mods in subject order (SPEC-0021).
    std::ranges::sort(packages, {}, &rawframe::content::BuildReference::subject);
    std::ranges::sort(mods, {}, &rawframe::content::BuildReference::subject);
    const auto kText =
        rawframe::content::writeComposition(rawframe::content::CompositionRecord{.game = *kGame,
                                                                                 .mods = std::move(mods),
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

/// Writes `text` to `path` under `mirror`, its directories made, and its
/// signature beside it.
bool publish(const std::filesystem::path& mirror,
             const std::string& path,
             std::string_view text,
             const rawframe::build::PublisherKey& key) {
    const auto kSigned = rawframe::build::sign(key, std::as_bytes(std::span{text.data(), text.size()}));
    if (!kSigned.has_value()) {
        print(kSigned.error());
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories((mirror / path).parent_path(), error);
    std::ofstream{mirror / path, std::ios::binary} << text;
    std::ofstream{mirror / (path + std::string{rawframe::content::kSignatureSuffix}), std::ios::binary}
        << rawframe::signature::writeEnvelope(*kSigned);
    return std::filesystem::is_regular_file(mirror / path, error);
}

/// The channel's pointer the mirror holds now, if any; the caller has
/// written it, so it is read, not verified.
std::optional<rawframe::release::ChannelPointer>
pointerIn(const std::filesystem::path& mirror, std::string_view subject, rawframe::release::Channel channel) {
    const std::filesystem::path kPath =
        mirror / rawframe::content::channelPathOf(subject, rawframe::release::nameOf(channel));
    std::error_code error;
    if (!std::filesystem::is_regular_file(kPath, error)) {
        return std::nullopt;
    }
    auto pointer = rawframe::release::readPointer(readText(kPath));
    return pointer.has_value() ? std::optional{std::move(*pointer)} : std::nullopt;
}

/// Points `subject`'s `channel` in `mirror` at `release`, the sequence after
/// the one there.
int point(const std::filesystem::path& mirror,
          std::string_view subject,
          rawframe::release::Channel channel,
          const rawframe::base::Sha256Digest& release,
          const rawframe::build::PublisherKey& key) {
    const auto kBefore = pointerIn(mirror, subject, channel);
    const rawframe::release::ChannelPointer kPointer{.subject = std::string{subject},
                                                     .channel = channel,
                                                     .release = release,
                                                     .sequence = kBefore.has_value() ? kBefore->sequence + 1 : 1,
                                                     .updatedAt = unixNow()};
    const auto kText = rawframe::release::writePointer(kPointer);
    if (!kText.has_value()) {
        print(kText.error());
        return 1;
    }
    if (!publish(mirror, rawframe::content::channelPathOf(subject, rawframe::release::nameOf(channel)), *kText, key)) {
        return 1;
    }
    std::printf("release %s on %.*s, sequence %lld\n",
                rawframe::content::ContentDigest{.bytes = release}.text().c_str(),
                static_cast<int>(rawframe::release::nameOf(channel).size()),
                rawframe::release::nameOf(channel).data(),
                static_cast<long long>(kPointer.sequence));
    return 0;
}

/// The key in the file at `path`, or none, said why.
std::optional<rawframe::build::PublisherKey> keyAt(const std::filesystem::path& path) {
    auto key = rawframe::build::readPublisherKey(readText(path));
    if (!key.has_value()) {
        print(key.error());
        return std::nullopt;
    }
    return std::move(*key);
}

int release(const std::filesystem::path& mirror,
            const std::filesystem::path& recordPath,
            std::string_view version,
            std::string_view channelName,
            const std::filesystem::path& keyPath) {
    const auto kChannel = rawframe::release::channelNamed(channelName);
    const std::string kRecordText = readText(recordPath);
    const auto kRecord = rawframe::content::readComposition(kRecordText);
    const auto kKey = keyAt(keyPath);
    if (!kChannel.has_value() || !kRecord.has_value() || !kKey.has_value()) {
        std::fputs("rawframe-build: release: a channel (stable, beta, nightly), a CompositionRecord, and a key\n",
                   stderr);
        return 1;
    }
    const rawframe::base::Sha256Digest kId = rawframe::content::compositionIdOf(kRecordText);
    std::error_code error;
    std::filesystem::create_directories((mirror / rawframe::content::compositionPathOf(kId)).parent_path(), error);
    std::ofstream{mirror / rawframe::content::compositionPathOf(kId), std::ios::binary} << kRecordText;
    const std::string& kSubject = kRecord->game.subject;
    const auto kBefore = pointerIn(mirror, kSubject, *kChannel);
    const rawframe::release::ReleaseRecord kRelease{
        .subject = kSubject,
        .version = std::string{version},
        .createdAt = unixNow(),
        .artifacts = {rawframe::release::Artifact{.platform = "any",
                                                  .mediaType = std::string{rawframe::release::kCompositionMediaType},
                                                  .size = kRecordText.size(),
                                                  .digest = kId}},
        .receipts = {},
        .predecessor = kBefore.has_value() ? std::optional{kBefore->release} : std::nullopt};
    const auto kText = rawframe::release::writeRelease(kRelease);
    if (!kText.has_value()) {
        print(kText.error());
        return 1;
    }
    const rawframe::base::Sha256Digest kReleaseId = rawframe::release::releaseIdOf(*kText);
    if (!publish(mirror, rawframe::content::releasePathOf(kReleaseId), *kText, *kKey)) {
        return 1;
    }
    return point(mirror, kSubject, *kChannel, kReleaseId, *kKey);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 7 && std::string_view{argv[1]} == "release") {
        return release(argv[2], argv[3], argv[4], argv[5], argv[6]);
    }
    if (argc == 7 && std::string_view{argv[1]} == "point") {
        const auto kChannel = rawframe::release::channelNamed(argv[4]);
        const auto kRelease = rawframe::content::ContentDigest::parse(argv[5]);
        const auto kKey = keyAt(argv[6]);
        if (!kChannel.has_value() || !kRelease.has_value() || !kKey.has_value()) {
            std::fputs("rawframe-build: point: a channel (stable, beta, nightly), a Release digest, and a key\n",
                       stderr);
            return 1;
        }
        return point(argv[2], argv[3], *kChannel, kRelease->bytes, *kKey);
    }
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
                   "       rawframe-build compose <library> <game root> <profile> <record> "
                   "[<package root> | --mod <mod root>]...\n"
                   "       rawframe-build release <mirror> <record> <version> <channel> <key>\n"
                   "       rawframe-build point <mirror> <subject> <channel> <release> <key>\n",
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
