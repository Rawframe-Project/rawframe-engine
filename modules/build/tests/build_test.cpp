// Packing a Build from a cook's output (SPEC-0021, ADR-0024): only a
// zero-failure receipt that names the manifest beside it is proof, every
// artifact must be what both say, blobs are stored by digest and reused,
// the BuildManifest is a canonical record, and the root hash is the
// identity section's alone.

#include "cooked.h"
#include "rawframe/build/build.h"
#include "rawframe/build/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/store.h"
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
using namespace rawframe::build::testing;

namespace {

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
    RAWFRAME_EXPECT(kFirst->resources == 3 && kFirst->blobsWritten >= 4 && kFirst->blobsReused == 0);
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

RAWFRAME_TEST(ABuildReadsBackAsItsResources) {
    const Cooked kCooked;
    const auto kKey = generatePublisherKey("rawframe");
    RAWFRAME_EXPECT(kKey.has_value());
    if (!kKey.has_value()) {
        return;
    }
    const auto kPacked = kCooked.pack(kIdentity, &*kKey);
    RAWFRAME_EXPECT(kPacked.has_value());
    if (!kPacked.has_value()) {
        return;
    }
    // The repeated phrase is stored as Zstandard frames far smaller than
    // it; the noise stays raw.
    const auto kRecord = document::parseCanonicalRecord(readText(kCooked.output / "build.manifest"));
    RAWFRAME_EXPECT(kRecord.has_value());
    if (!kRecord.has_value()) {
        return;
    }
    const auto kList = [&kRecord](std::uint64_t id) {
        std::array<char, 32> hex{};
        base::formatBits128Hex(base::Bits128{.high = 0, .low = id}, hex);
        return kRecord->find("chunks")->find(std::string_view{hex.data(), hex.size()})->items();
    };
    std::int64_t stored = 0;
    for (const document::Value& chunk : kList(3)) {
        RAWFRAME_EXPECT(*chunk.find("codec")->text() == "zstd");
        stored += *chunk.find("blob_size")->integer();
    }
    RAWFRAME_EXPECT(stored * 20 < static_cast<std::int64_t>(kCooked.repeated.size()));
    RAWFRAME_EXPECT(*kList(2)[0].find("codec")->text() == "raw");

    // Read back through the runtime's reader, every resource is itself.
    const auto kKeys = keySetOf(*kKey, 1'790'000'000);
    RAWFRAME_EXPECT(kKeys.has_value());
    auto opened =
        content::ContentSource::build(kCooked.output, kPacked->root, kKeys.value_or(signature::PublisherKeySet{}));
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    execution::ManualClock clock;
    execution::CancellationScope scope{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 8}).has_value());
    {
        std::vector<content::ContentSource> sources;
        sources.push_back(std::move(opened->source));
        auto store =
            std::move(*content::ContentStore::create(io, execution::OwnerId{1}, scope, clock, std::move(sources)));
        const content::AdmittedRepresentation kWave{.type = kCooked.entries[0].type,
                                                    .representation = kCooked.entries[0].representation};
        const std::vector<content::BoundManifest> kManifests = {{.entries = opened->entries, .source = 0}};
        store->publish(*content::ContentCatalog::build(kManifests, std::span{&kWave, 1}, 1, 1));
        const auto kRead = [&store](std::uint64_t id) -> std::string {
            auto read =
                store->read(content::ResourceRef{.id = content::ResourceId{base::Bits128{.high = 0, .low = id}},
                                                 .type = content::ResourceTypeId{base::Bits128{.high = 9, .low = 9}}});
            if (!read.has_value()) {
                return "";
            }
            auto outcome = read->wait();
            if (!outcome.hasValue()) {
                return "";
            }
            const std::span<const std::byte> kBytes = (*outcome).bytes();
            return std::string{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()};
        };
        RAWFRAME_EXPECT(kRead(1) == "bang" && kRead(2) == kCooked.large && kRead(3) == kCooked.repeated);
    }
    io.stop();
}

RAWFRAME_TEST(APublisherKeySignsItsOwnBuilds) {
    const auto kKey = generatePublisherKey("rawframe");
    RAWFRAME_EXPECT(kKey.has_value() && signature::validKeyId(kKey->kid));
    if (!kKey.has_value()) {
        return;
    }
    // Its file reads back as itself; a publisher outside its grammar is
    // refused a key.
    const auto kRead = readPublisherKey(writePublisherKey(*kKey));
    RAWFRAME_EXPECT(kRead.has_value() && kRead->kid == kKey->kid && kRead->seed == kKey->seed);
    RAWFRAME_EXPECT(!generatePublisherKey("Rawframe").has_value() && !readPublisherKey("{}").has_value());
    // Its key set lists it active and reads back through the verifier's
    // reader.
    const auto kKeys = keySetOf(*kKey, 1'790'000'000);
    RAWFRAME_EXPECT(kKeys.has_value());
    if (!kKeys.has_value()) {
        return;
    }
    const auto kKeysText = signature::writePublisherKeySet(*kKeys);
    RAWFRAME_EXPECT(kKeysText.has_value() && signature::readPublisherKeySet(*kKeysText).has_value());

    // A signed Build verifies against it; another publisher's key signs
    // nothing of this one's.
    const Cooked kCooked;
    const auto kPacked = kCooked.pack(kIdentity, &*kKey);
    RAWFRAME_EXPECT(kPacked.has_value());
    const std::string kManifest = readText(kCooked.output / "build.manifest");
    const auto kEnvelope = signature::readEnvelope(readText(kCooked.output / "build.manifest.sig"));
    RAWFRAME_EXPECT(kEnvelope.has_value() && kEnvelope->kid == kKey->kid &&
                    signature::verifyPublished(*kKeys, bytesOf(kManifest), *kEnvelope).has_value());
    const auto kOther = generatePublisherKey("someone");
    RAWFRAME_EXPECT(kOther.has_value() && refusedAs(kCooked.pack(kIdentity, &*kOther), BuildError::BadKey));
    // Packing again unsigned leaves no stale signature beside the manifest.
    RAWFRAME_EXPECT(kCooked.pack().has_value() && !fs::exists(kCooked.output / "build.manifest.sig"));
}
