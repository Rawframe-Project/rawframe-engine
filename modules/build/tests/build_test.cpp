// Packing a Build from a cook's output (SPEC-0021, ADR-0024): only a
// zero-failure receipt that names the manifest beside it is proof, every
// artifact must be what both say, blobs are stored by digest and reused,
// the BuildManifest is a canonical record, and the root hash is the
// identity section's alone.

#include "rawframe/build/build.h"
#include "rawframe/build/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/document/json.h"
#include "rawframe/test/test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using namespace rawframe::build;

namespace {

namespace fs = std::filesystem;

std::string readText(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

void writeText(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

std::span<const std::byte> bytesOf(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

/// Bytes with no structure a chunker could find, `size` long.
std::string noise(std::size_t size) {
    std::string text;
    for (std::uint64_t block = 0; text.size() < size; ++block) {
        const base::Sha256Digest kDigest = base::sha256(std::to_string(block));
        text.append(reinterpret_cast<const char*>(kDigest.data()), kDigest.size());
    }
    text.resize(size);
    return text;
}

const BuildIdentity kIdentity{.subject = "rawframe/runners",
                              .version = "0.1.0",
                              .engine = "0.1.0",
                              .platform = "linux",
                              .architecture = "x86_64",
                              .side = "client",
                              .configuration = "build.development",
                              .profile = "tool"};

/// A cook's output of a small sound and a three-megabyte one, with its
/// manifest and a receipt that proves it.
struct Cooked {
    fs::path base = fs::temp_directory_path() / ("rawframe-build-" + std::to_string(::getpid()));
    fs::path cooked = base / "cooked";
    fs::path output = base / "build";
    std::vector<content::ManifestEntry> entries;
    std::string large = noise(std::size_t{3} * 1024 * 1024);

    Cooked() {
        fs::remove_all(base);
        add(1, "bang");
        add(2, large);
        prove(0);
    }
    ~Cooked() {
        fs::remove_all(base);
    }
    Cooked(const Cooked&) = delete;
    Cooked& operator=(const Cooked&) = delete;

    void add(std::uint64_t id, std::string_view bytes) {
        const content::ContentDigest kDigest = content::ContentDigest::of(bytesOf(bytes));
        const std::string kLocator = "objects/" + kDigest.text().substr(7);
        writeText(cooked / kLocator, bytes);
        entries.push_back(
            content::ManifestEntry{.id = content::ResourceId{base::Bits128{.high = 0, .low = id}},
                                   .type = content::ResourceTypeId{base::Bits128{.high = 9, .low = 9}},
                                   .representation = *content::RepresentationId::parse("rawframe.audio.wave"),
                                   .byteLength = bytes.size(),
                                   .digest = kDigest,
                                   .locator = kLocator});
    }

    /// The manifest, and a receipt of `failures` naming it.
    void prove(std::int64_t failures, std::string_view named = {}) const {
        const std::string kManifest = content::writeManifest(entries);
        writeText(cooked / "content.manifest", kManifest);
        document::Value artifacts = document::Value::array();
        for (const content::ManifestEntry& entry : entries) {
            std::array<char, 32> id{};
            base::formatBits128Hex(entry.id.value, id);
            document::Value artifact = document::Value::object();
            artifact.add("resourceId", document::Value::string(std::string{id.data(), id.size()}));
            artifact.add("representation", document::Value::string(std::string{entry.representation.text()}));
            artifact.add("digest", document::Value::string(entry.digest.text()));
            artifact.add("byteLength", document::Value::integer(static_cast<std::int64_t>(entry.byteLength)));
            artifacts.push(std::move(artifact));
        }
        document::Value receipt = document::Value::object();
        receipt.add("kind", document::Value::string("cook.receipt"));
        receipt.add("formatVersion", document::Value::integer(1));
        receipt.add("manifest",
                    document::Value::string(named.empty() ? content::ContentDigest::of(bytesOf(kManifest)).text()
                                                          : std::string{named}));
        receipt.add("artifacts", std::move(artifacts));
        receipt.add("failures", document::Value::integer(failures));
        writeText(cooked / "cook.receipt", document::write(receipt));
    }

    [[nodiscard]] result::Result<BuildReport> pack(BuildIdentity identity = kIdentity) const {
        return packBuild(BuildRequest{.cooked = cooked, .output = output, .identity = std::move(identity)});
    }
};

bool refusedAs(const result::Result<BuildReport>& report, BuildError error) {
    return !report.has_value() && report.error().domain() == kBuildDomain && report.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(ABuildIsPackedFromProvenArtifacts) {
    const Cooked kCooked;
    const auto kFirst = kCooked.pack();
    RAWFRAME_EXPECT(kFirst.has_value());
    if (!kFirst.has_value()) {
        return;
    }
    // The large sound is several chunks, the small one one: every blob
    // written once, then reused by a second pack of the same bytes.
    RAWFRAME_EXPECT(kFirst->resources == 2 && kFirst->blobsWritten >= 3 && kFirst->blobsReused == 0);
    const auto kSecond = kCooked.pack();
    RAWFRAME_EXPECT(kSecond.has_value() && kSecond->root == kFirst->root && kSecond->blobsWritten == 0 &&
                    kSecond->blobsReused == kFirst->blobsWritten);

    // The manifest is a canonical record whose digest the report gives, and
    // the root hash is its identity section's.
    const std::string kManifest = readText(kCooked.output / "build.manifest");
    const auto kRecord = document::parseCanonicalRecord(kManifest);
    RAWFRAME_EXPECT(kRecord.has_value() &&
                    content::sameDigest(content::ContentDigest::of(bytesOf(kManifest)), kFirst->manifest));
    if (kRecord.has_value()) {
        const auto kIdentityBytes = document::writeCanonicalRecord(*kRecord->find("identity"));
        RAWFRAME_EXPECT(kIdentityBytes.has_value() && base::sha256(bytesOf(*kIdentityBytes)) == kFirst->root);
        // The large sound's chunks cover it exactly, each blob its content.
        std::array<char, 32> id{};
        base::formatBits128Hex(base::Bits128{.high = 0, .low = 2}, id);
        const document::Value* list = kRecord->find("chunks")->find(std::string_view{id.data(), id.size()});
        RAWFRAME_EXPECT(list != nullptr && list->items().size() >= 2);
        std::string joined;
        for (const document::Value& chunk : list != nullptr ? list->items() : std::span<const document::Value>{}) {
            const std::string kHex = chunk.find("blob")->text()->substr(7);
            joined += readText(kCooked.output / "sha256" / kHex.substr(0, 2) / kHex.substr(2));
            RAWFRAME_EXPECT(*chunk.find("codec")->text() == "raw" &&
                            *chunk.find("content")->text() == *chunk.find("blob")->text());
        }
        RAWFRAME_EXPECT(joined == kCooked.large);
    }
    // The packaging receipt names the manifest by digest.
    const auto kReceipt = document::parseCanonicalRecord(readText(kCooked.output / "packaging.receipt"));
    RAWFRAME_EXPECT(kReceipt.has_value() && *kReceipt->find("manifest")->text() == kFirst->manifest.text());

    // Another version is another Build; the same bytes, the same blobs.
    BuildIdentity next = kIdentity;
    next.version = "0.2.0-beta.1+linux";
    const auto kNext = kCooked.pack(next);
    RAWFRAME_EXPECT(kNext.has_value() && kNext->root != kFirst->root && kNext->blobsWritten == 0);
}

RAWFRAME_TEST(OnlyAProofIsPacked) {
    Cooked cooked;
    // A receipt with a failure, one naming another manifest, and none.
    cooked.prove(1);
    RAWFRAME_EXPECT(refusedAs(cooked.pack(), BuildError::NoProof));
    cooked.prove(0, "sha256:0000000000000000000000000000000000000000000000000000000000000000");
    RAWFRAME_EXPECT(refusedAs(cooked.pack(), BuildError::NoProof));
    fs::remove(cooked.cooked / "cook.receipt");
    RAWFRAME_EXPECT(refusedAs(cooked.pack(), BuildError::NoProof));
    // An artifact whose bytes changed after the cook.
    cooked.prove(0);
    writeText(cooked.cooked / cooked.entries[0].locator, "bong");
    RAWFRAME_EXPECT(refusedAs(cooked.pack(), BuildError::ArtifactMismatch));
    writeText(cooked.cooked / cooked.entries[0].locator, "bang");
    // Nothing was published by any refusal.
    RAWFRAME_EXPECT(!fs::exists(cooked.output / "build.manifest"));
    RAWFRAME_EXPECT(cooked.pack().has_value());
    // A Build inside the cook's output is refused.
    RAWFRAME_EXPECT(refusedAs(
        packBuild(BuildRequest{.cooked = cooked.cooked, .output = cooked.cooked / "build", .identity = kIdentity}),
        BuildError::BadRequest));
}

RAWFRAME_TEST(IdentityFieldsKeepTheirGrammar) {
    const Cooked kCooked;
    const auto kWith = [&kCooked](auto change) {
        BuildIdentity identity = kIdentity;
        change(identity);
        return refusedAs(kCooked.pack(identity), BuildError::BadIdentity);
    };
    RAWFRAME_EXPECT(kWith([](BuildIdentity& each) {
        each.subject = "Rawframe/runners";
    }));
    RAWFRAME_EXPECT(kWith([](BuildIdentity& each) {
        each.subject = "runners";
    }));
    RAWFRAME_EXPECT(kWith([](BuildIdentity& each) {
        each.subject = "rawframe/-runners";
    }));
    RAWFRAME_EXPECT(kWith([](BuildIdentity& each) {
        each.version = "1.0";
    }));
    RAWFRAME_EXPECT(kWith([](BuildIdentity& each) {
        each.engine = "01.0.0";
    }));
    RAWFRAME_EXPECT(kWith([](BuildIdentity& each) {
        each.side = "both";
    }));
    RAWFRAME_EXPECT(kWith([](BuildIdentity& each) {
        each.configuration = "debug";
    }));
    RAWFRAME_EXPECT(kWith([](BuildIdentity& each) {
        each.platform = "Linux";
    }));
    for (const std::string_view kGood : {"0.0.0", "1.2.3-rc.1", "1.2.3+build.5", "10.20.30-alpha-1.0+001"}) {
        RAWFRAME_EXPECT(validVersion(kGood));
    }
    for (const std::string_view kBad : {"1.2", "1.2.3.4", "1.2.3-01", "1.2.3-", "1.2.3+", "v1.2.3", "1.2.3-a..b"}) {
        RAWFRAME_EXPECT(!validVersion(kBad));
    }
}
