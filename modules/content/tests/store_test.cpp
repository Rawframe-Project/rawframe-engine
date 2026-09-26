// The store against SPEC-0008's read algorithm: bytes returned only when
// exactly the declared length and digest, typed and pinned refusals before
// any input or output, memory and directory sources alike, a directory
// source no link can lead out of, reads that finish in the generation they
// began in, and cancellation and deadlines that stop publication.

#include "rawframe/base/platform.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/store.h"
#include "rawframe/document/json.h"
#include "rawframe/signature/errors.h"
#include "rawframe/test/files.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"

#include <iterator>
#include <string>
#include <tuple>
#include <vector>

// Directories and Builds on disk, where there are files; the web build runs
// the store over memory sources alone.
#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>
#include <zstd.h>
#endif

using namespace rawframe;
using namespace rawframe::content;

namespace {

constexpr ResourceTypeId kSoundType{base::Bits128{.high = 0x50, .low = 1}};
constexpr ResourceTypeId kTextureType{base::Bits128{.high = 0x50, .low = 2}};
constexpr execution::OwnerId kOwner{11};

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto kBytes = std::as_bytes(std::span{text.data(), text.size()});
    return {kBytes.begin(), kBytes.end()};
}

ResourceId idOf(std::uint64_t value) {
    return ResourceId{base::Bits128{.high = 0, .low = value}};
}

ManifestEntry entry(std::uint64_t id, std::string locator, std::string_view bytes) {
    const std::vector<std::byte> kBytes = bytesOf(bytes);
    return ManifestEntry{.id = idOf(id),
                         .type = kSoundType,
                         .representation = *RepresentationId::parse("rawframe.audio.opus"),
                         .byteLength = kBytes.size(),
                         .digest = ContentDigest::of(kBytes),
                         .locator = std::move(locator)};
}

std::shared_ptr<const ContentCatalog> catalogOf(std::vector<ManifestEntry> entries, std::uint64_t generation) {
    const std::vector<AdmittedRepresentation> kAdmitted = {
        {.type = kSoundType, .representation = *RepresentationId::parse("rawframe.audio.opus")}};
    const std::vector<BoundManifest> kManifests = {BoundManifest{.entries = std::move(entries), .source = 0}};
    return *ContentCatalog::build(kManifests, kAdmitted, 1, generation);
}

/// An executor, a scope, and a store over one source, as a Runtime would
/// own them.
struct Fixture {
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 2}};
    std::unique_ptr<ContentStore> store;

    explicit Fixture(ContentSource source) {
        RAWFRAME_EXPECT(io.admitOwner(kOwner, {.maximumPendingTasks = 64}).has_value());
        std::vector<ContentSource> sources;
        sources.push_back(std::move(source));
        store = std::move(*ContentStore::create(io, kOwner, root, clock, std::move(sources)));
    }
    ~Fixture() {
        store.reset();
        io.stop();
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

/// The read's outcome, waited for; the error's code, or nought for bytes.
std::pair<std::uint32_t, std::string> outcomeOf(result::Result<execution::AsyncHandle<VerifiedContent>> started) {
    if (!started.has_value()) {
        return {started.error().code().value, "refused"};
    }
    auto outcome = started->wait();
    if (outcome.isCancelled()) {
        return {code(ContentError::Cancelled).value, "cancelled"};
    }
    if (outcome.isError()) {
        return {outcome.error().code().value, "failed"};
    }
    const std::span<const std::byte> kBytes = (*outcome).bytes();
    return {0, std::string{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()}};
}

#if RAWFRAME_FILE_SYSTEM
std::pair<std::uint32_t, std::string> read(std::string_view bytes) {
    return {0, std::string{bytes}};
}
#endif

std::pair<std::uint32_t, std::string> failure(ContentError error, std::string_view how) {
    return {code(error).value, std::string{how}};
}

} // namespace

RAWFRAME_TEST(AReadReturnsExactlyTheDeclaredBytes) {
    auto source = ContentSource::memory({{"shot.rfopus", bytesOf("bang")},
                                         {"wrong.rfopus", bytesOf("bong")},
                                         {"short.rfopus", bytesOf("ban")},
                                         {"long.rfopus", bytesOf("banging")}});
    Fixture fixture{std::move(*source)};
    ContentStore& store = *fixture.store;
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kSoundType})) ==
                    failure(ContentError::SourceUnavailable, "refused"));
    store.publish(catalogOf({entry(1, "shot.rfopus", "bang"),
                             entry(2, "wrong.rfopus", "bang"),
                             entry(3, "short.rfopus", "bang"),
                             entry(4, "long.rfopus", "bang"),
                             entry(5, "missing.rfopus", "bang")},
                            1));
    const ResourceRef kShot{.id = idOf(1), .type = kSoundType};
    auto started = store.read(kShot);
    RAWFRAME_EXPECT(started.has_value());
    if (started.has_value()) {
        auto outcome = started->wait();
        RAWFRAME_EXPECT(outcome.hasValue() && (*outcome).bytes().size() == 4 && (*outcome).generation() == 1 &&
                        (*outcome).descriptor().id == idOf(1) &&
                        (*outcome).fingerprint() == store.catalog()->fingerprint());
    }
    // Refused before any read.
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kTextureType})) ==
                    failure(ContentError::ResourceTypeMismatch, "refused"));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(9), .type = kSoundType})) ==
                    failure(ContentError::ResourceNotFound, "refused"));
    RAWFRAME_EXPECT(outcomeOf(store.read(kShot, {.maximumBytes = 3})) ==
                    failure(ContentError::ResourceTooLarge, "refused"));
    ContentDigest other = entry(1, "x", "bong").digest;
    RAWFRAME_EXPECT(outcomeOf(store.read(PinnedResourceRef{.resource = kShot, .digest = other})) ==
                    failure(ContentError::RevisionMismatch, "refused"));
    RAWFRAME_EXPECT(
        outcomeOf(store.read(PinnedResourceRef{.resource = kShot, .digest = entry(1, "x", "bang").digest})).first == 0);
    // Read, and refused for what the bytes turned out to be.
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(2), .type = kSoundType})) ==
                    failure(ContentError::DigestMismatch, "failed"));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(3), .type = kSoundType})) ==
                    failure(ContentError::ShortRead, "failed"));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(4), .type = kSoundType})) ==
                    failure(ContentError::SourceChanged, "failed"));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(5), .type = kSoundType})) ==
                    failure(ContentError::ReadFailed, "failed"));
    RAWFRAME_EXPECT(!ContentSource::memory({{"../up", {}}}).has_value());
}

#if RAWFRAME_FILE_SYSTEM
RAWFRAME_TEST(ADirectorySourceReadsOnlyWithinItsRoot) {
    const std::filesystem::path kBase = test::scratchDirectory("content");
    const std::filesystem::path kRoot = kBase / "root";
    std::filesystem::create_directories(kRoot / "sounds");
    std::filesystem::create_directories(kBase / "outside");
    std::ofstream{kRoot / "sounds" / "shot.rfopus"} << "bang";
    std::ofstream{kBase / "outside" / "secret.rfopus"} << "bang";
    // A directory link and a file link, both out of the root.
    std::filesystem::create_directory_symlink(kBase / "outside", kRoot / "escape");
    std::filesystem::create_symlink(kBase / "outside" / "secret.rfopus", kRoot / "sounds" / "linked.rfopus");
    {
        Fixture fixture{std::move(*ContentSource::directory(kRoot))};
        ContentStore& store = *fixture.store;
        store.publish(catalogOf({entry(1, "sounds/shot.rfopus", "bang"),
                                 entry(2, "escape/secret.rfopus", "bang"),
                                 entry(3, "sounds/linked.rfopus", "bang"),
                                 entry(4, "sounds/none.rfopus", "bang")},
                                1));
        RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kSoundType})) == read("bang"));
        RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(2), .type = kSoundType})) ==
                        failure(ContentError::PathEscape, "failed"));
        RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(3), .type = kSoundType})) ==
                        failure(ContentError::PathEscape, "failed"));
        RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(4), .type = kSoundType})) ==
                        failure(ContentError::ReadFailed, "failed"));
    }
    // The same catalog over the same bytes in memory reads the same.
    Fixture inMemory{std::move(*ContentSource::memory({{"sounds/shot.rfopus", bytesOf("bang")}}))};
    inMemory.store->publish(catalogOf({entry(1, "sounds/shot.rfopus", "bang")}, 1));
    RAWFRAME_EXPECT(outcomeOf(inMemory.store->read(ResourceRef{.id = idOf(1), .type = kSoundType})) == read("bang"));
    RAWFRAME_EXPECT(!ContentSource::directory(kBase / "nowhere").has_value());
    std::filesystem::remove_all(kBase);
}
#endif

RAWFRAME_TEST(AReadFinishesInTheGenerationItBeganIn) {
    Fixture fixture{std::move(*ContentSource::memory({{"a.rfopus", bytesOf("one")}, {"b.rfopus", bytesOf("two")}}))};
    ContentStore& store = *fixture.store;
    store.publish(catalogOf({entry(1, "a.rfopus", "one")}, 1));
    auto first = store.read(ResourceRef{.id = idOf(1), .type = kSoundType});
    // Replaced while the first read may still run: the same resource now
    // lives elsewhere with other bytes.
    store.publish(catalogOf({entry(1, "b.rfopus", "two")}, 2));
    auto second = store.read(ResourceRef{.id = idOf(1), .type = kSoundType});
    RAWFRAME_EXPECT(first.has_value() && second.has_value());
    if (first.has_value() && second.has_value()) {
        auto firstOutcome = first->wait();
        auto secondOutcome = second->wait();
        RAWFRAME_EXPECT(firstOutcome.hasValue() && (*firstOutcome).generation() == 1 &&
                        (*firstOutcome).bytes().size() == 3 &&
                        std::to_integer<char>((*firstOutcome).bytes()[0]) == 'o');
        RAWFRAME_EXPECT(secondOutcome.hasValue() && (*secondOutcome).generation() == 2 &&
                        std::to_integer<char>((*secondOutcome).bytes()[0]) == 't');
    }
    // Many at once, on two workers, all verified.
    std::vector<execution::AsyncHandle<VerifiedContent>> many;
    for (int read = 0; read < 32; ++read) {
        auto started = store.read(ResourceRef{.id = idOf(1), .type = kSoundType});
        if (started.has_value()) {
            many.push_back(std::move(*started));
        }
    }
    bool allVerified = many.size() == 32;
    for (auto& handle : many) {
        allVerified = allVerified && handle.wait().hasValue();
    }
    RAWFRAME_EXPECT(allVerified);
}

RAWFRAME_TEST(CancellationAndDeadlinesStopPublication) {
    Fixture fixture{std::move(*ContentSource::memory({{"a.rfopus", bytesOf("one")}}))};
    ContentStore& store = *fixture.store;
    store.publish(catalogOf({entry(1, "a.rfopus", "one")}, 1));
    // A deadline already passed: the bytes are never handed out.
    fixture.clock.advance(execution::MonotonicDuration::fromMilliseconds(10));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kSoundType},
                                         {.deadline = execution::MonotonicInstant{0}})) ==
                    failure(ContentError::DeadlineExceeded, "failed"));
    // The Runtime cancelling: refused or cancelled, never published.
    fixture.root.cancel(execution::CancelReason::Requested);
    const auto kCancelled = outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kSoundType}));
    RAWFRAME_EXPECT(kCancelled.first != 0);
}

#if RAWFRAME_FILE_SYSTEM
namespace {

/// The test publisher's key: a fixed seed, and the key set that lists it.
struct TestPublisher {
    EVP_PKEY* key = nullptr;
    signature::PublisherKeySet keys;

    TestPublisher() {
        std::array<unsigned char, 32> seed{};
        seed.fill(0x42);
        key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
        signature::PublicKey publicKey{};
        std::size_t length = publicKey.size();
        EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char*>(publicKey.data()), &length);
        keys = signature::PublisherKeySet{
            .publisher = "rawframe",
            .sequence = 1,
            .updatedAt = 1,
            .head = "sha256:" + std::string(64, '0'),
            .keys = {signature::PublisherKey{
                .kid = "0000000000000001", .publicKey = publicKey, .state = signature::KeyState::Active, .since = 1}}};
    }
    ~TestPublisher() {
        EVP_PKEY_free(key);
    }
    TestPublisher(const TestPublisher&) = delete;
    TestPublisher& operator=(const TestPublisher&) = delete;

    /// Writes `manifest` and its signature envelope into `root`.
    void publish(const std::filesystem::path& root, std::string_view manifest) const {
        std::ofstream{root / "build.manifest", std::ios::binary} << manifest;
        signature::Envelope envelope{.kid = "0000000000000001", .sig = {}};
        std::size_t length = envelope.sig.size();
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key);
        EVP_DigestSign(context,
                       reinterpret_cast<unsigned char*>(envelope.sig.data()),
                       &length,
                       reinterpret_cast<const unsigned char*>(manifest.data()),
                       manifest.size());
        EVP_MD_CTX_free(context);
        std::ofstream{root / "build.manifest.sig", std::ios::binary} << signature::writeEnvelope(envelope);
    }
};

/// A Build on disk as SPEC-0021 lays it out: resource 1 "bang" in one raw
/// chunk, resource 2 "abcdefgh" in two ("abcd", "efgh"). `chunksOf2`
/// overrides resource 2's chunk texts in the manifest, `codec` every
/// chunk's codec.
struct BuildOnDisk {
    TestPublisher publisher;
    std::filesystem::path root = test::scratchDirectory("content-build");
    base::Sha256Digest rootHash{};

    explicit BuildOnDisk(std::vector<std::string> chunksOf2 = {"abcd", "efgh"}, std::string_view codec = "raw") {
        std::filesystem::remove_all(root);
        for (const std::string_view kText : {"bang", "abcd", "efgh"}) {
            write(kText);
        }
        const auto kChunkList = [codec](const std::vector<std::string>& texts) {
            document::Value list = document::Value::array();
            for (const std::string& text : texts) {
                const std::string kDigest = ContentDigest::of(bytesOf(text)).text();
                document::Value chunk = document::Value::object();
                chunk.add("content", document::Value::string(kDigest));
                chunk.add("size", document::Value::integer(static_cast<std::int64_t>(text.size())));
                chunk.add("blob", document::Value::string(kDigest));
                chunk.add("blob_size", document::Value::integer(static_cast<std::int64_t>(text.size())));
                chunk.add("codec", document::Value::string(std::string{codec}));
                list.push(std::move(chunk));
            }
            return list;
        };
        document::Value resources = document::Value::array();
        document::Value chunks = document::Value::object();
        for (const auto& [kId, kText, kParts] : {std::tuple{1, std::string{"bang"}, std::vector<std::string>{"bang"}},
                                                 std::tuple{2, std::string{"abcdefgh"}, chunksOf2}}) {
            std::array<char, 32> hex{};
            base::formatBits128Hex(idOf(static_cast<std::uint64_t>(kId)).value, hex);
            const std::string kHex{hex.data(), hex.size()};
            std::array<char, 32> type{};
            base::formatBits128Hex(kSoundType.value, type);
            document::Value resource = document::Value::object();
            resource.add("resource", document::Value::string(kHex));
            resource.add("type", document::Value::string(std::string{type.data(), type.size()}));
            resource.add("representation", document::Value::string("rawframe.audio.opus"));
            resource.add("digest", document::Value::string(ContentDigest::of(bytesOf(kText)).text()));
            resource.add("size", document::Value::integer(static_cast<std::int64_t>(kText.size())));
            resources.push(std::move(resource));
            chunks.add(kHex, kChunkList(kParts));
        }
        document::Value identity = document::Value::object();
        identity.add("subject", document::Value::string("rawframe/test"));
        identity.add("version", document::Value::string("1.0.0"));
        identity.add("resources", std::move(resources));
        rootHash = base::sha256(*document::writeCanonicalRecord(identity));
        document::Value manifest = document::Value::object();
        manifest.add("schema", document::Value::integer(1));
        manifest.add("identity", std::move(identity));
        manifest.add("chunks", std::move(chunks));
        publisher.publish(root, *document::writeCanonicalRecord(manifest));
    }
    ~BuildOnDisk() {
        std::filesystem::remove_all(root);
    }
    BuildOnDisk(const BuildOnDisk&) = delete;
    BuildOnDisk& operator=(const BuildOnDisk&) = delete;

    [[nodiscard]] std::filesystem::path blobOf(std::string_view text) const {
        const std::string kHex = ContentDigest::of(bytesOf(text)).text().substr(7);
        return root / "sha256" / kHex.substr(0, 2) / kHex.substr(2);
    }
    void write(std::string_view text) const {
        std::filesystem::create_directories(blobOf(text).parent_path());
        std::ofstream{blobOf(text), std::ios::binary} << text;
    }
};

std::pair<std::uint32_t, std::string> readOfBuild(const BuildOnDisk& build, std::uint64_t id) {
    auto opened = ContentSource::build(build.root, build.rootHash, build.publisher.keys);
    if (!opened.has_value()) {
        return {opened.error().code().value, "refused"};
    }
    std::vector<BoundManifest> manifests = {BoundManifest{.entries = opened->entries, .source = 0}};
    const std::vector<AdmittedRepresentation> kAdmitted = {
        {.type = kSoundType, .representation = *RepresentationId::parse("rawframe.audio.opus")}};
    Fixture fixture{std::move(opened->source)};
    fixture.store->publish(*ContentCatalog::build(manifests, kAdmitted, 1, 1));
    return outcomeOf(fixture.store->read(ResourceRef{.id = idOf(id), .type = kSoundType}));
}

} // namespace

RAWFRAME_TEST(ABuildIsReadInItsVerificationOrder) {
    {
        const BuildOnDisk kBuild;
        const auto kOpened = ContentSource::build(kBuild.root, kBuild.rootHash, kBuild.publisher.keys);
        RAWFRAME_EXPECT(kOpened.has_value() && kOpened->subject == "rawframe/test" && kOpened->version == "1.0.0" &&
                        kOpened->entries.size() == 2 && kOpened->root == kBuild.rootHash);
        // Whole resources from their chunks, verified.
        RAWFRAME_EXPECT(readOfBuild(kBuild, 1) == read("bang") && readOfBuild(kBuild, 2) == read("abcdefgh"));
        // Not the Build that was named.
        base::Sha256Digest other = kBuild.rootHash;
        other[0] ^= std::byte{1};
        const auto kOther = ContentSource::build(kBuild.root, other, kBuild.publisher.keys);
        RAWFRAME_EXPECT(!kOther.has_value() && kOther.error().code() == code(ContentError::DigestMismatch));
        // A blob changed on disk: refused before its bytes are used.
        std::ofstream{kBuild.blobOf("abcd"), std::ios::binary} << "abce";
        RAWFRAME_EXPECT(readOfBuild(kBuild, 2) == failure(ContentError::DigestMismatch, "failed"));
        // A blob gone.
        std::filesystem::remove(kBuild.blobOf("bang"));
        RAWFRAME_EXPECT(readOfBuild(kBuild, 1).first == code(ContentError::ReadFailed).value);
    }
    {
        // Chunk lists are outside the root hash: swapped, each chunk is
        // itself, but the whole is not, and the whole is checked.
        const BuildOnDisk kSwapped{{"efgh", "abcd"}};
        RAWFRAME_EXPECT(readOfBuild(kSwapped, 2) == failure(ContentError::DigestMismatch, "failed"));
    }
    {
        // Chunks that do not cover the resource, and a codec this reader
        // does not take, are refused when the Build is opened.
        const BuildOnDisk kShort{{"abcd"}};
        RAWFRAME_EXPECT(readOfBuild(kShort, 2).first == code(ContentError::ManifestInvalid).value);
        const BuildOnDisk kZstd{{"abcd", "efgh"}, "zstd"};
        RAWFRAME_EXPECT(readOfBuild(kZstd, 1).first == code(ContentError::ManifestInvalid).value);
    }
    {
        // A manifest that is not a canonical record, though signed.
        const BuildOnDisk kBuild;
        std::ifstream file{kBuild.root / "build.manifest", std::ios::binary};
        const std::string kText{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        kBuild.publisher.publish(kBuild.root, kText + "\n");
        RAWFRAME_EXPECT(readOfBuild(kBuild, 1).first == code(ContentError::ManifestInvalid).value);
    }
}

RAWFRAME_TEST(ABuildHeldInMemoryIsReadAsOneOnDisk) {
    const BuildOnDisk kBuild;
    // Every file of the Build, as a web client holds what it fetched.
    const auto kHeld = [&kBuild] {
        std::vector<std::pair<std::string, std::vector<std::byte>>> files;
        for (const std::string& path : test::filesUnder(kBuild.root.string(), "")) {
            files.emplace_back(path, bytesOf(test::readFile((kBuild.root / path).string())));
        }
        return files;
    };
    auto opened = ContentSource::build(kHeld(), kBuild.rootHash, kBuild.publisher.keys);
    RAWFRAME_EXPECT(opened.has_value() && opened->entries.size() == 2 && opened->root == kBuild.rootHash);
    std::vector<BoundManifest> manifests = {BoundManifest{.entries = opened->entries, .source = 0}};
    const std::vector<AdmittedRepresentation> kAdmitted = {
        {.type = kSoundType, .representation = *RepresentationId::parse("rawframe.audio.opus")}};
    Fixture fixture{std::move(opened->source)};
    fixture.store->publish(*ContentCatalog::build(manifests, kAdmitted, 1, 1));
    RAWFRAME_EXPECT(outcomeOf(fixture.store->read(ResourceRef{.id = idOf(2), .type = kSoundType})) == read("abcdefgh"));

    // A blob changed in memory is refused before it is used; so is a Build
    // without its signature, and a path that is no locator.
    auto changed = kHeld();
    const std::string kBlob = kBuild.blobOf("abcd").lexically_relative(kBuild.root).generic_string();
    for (auto& [path, bytes] : changed) {
        if (path == kBlob) {
            bytes = bytesOf("abce");
        }
    }
    auto tampered = ContentSource::build(std::move(changed), kBuild.rootHash, kBuild.publisher.keys);
    Fixture second{std::move(tampered->source)};
    second.store->publish(*ContentCatalog::build(
        std::vector<BoundManifest>{BoundManifest{.entries = tampered->entries, .source = 0}}, kAdmitted, 2, 1));
    RAWFRAME_EXPECT(outcomeOf(second.store->read(ResourceRef{.id = idOf(2), .type = kSoundType})) ==
                    failure(ContentError::DigestMismatch, "failed"));
    auto unsigned_ = kHeld();
    std::erase_if(unsigned_, [](const auto& file) {
        return file.first == "build.manifest.sig";
    });
    const auto kUnsigned = ContentSource::build(std::move(unsigned_), kBuild.rootHash, kBuild.publisher.keys);
    RAWFRAME_EXPECT(!kUnsigned.has_value() && kUnsigned.error().code() == code(ContentError::SourceUnavailable));
    auto escaping = kHeld();
    escaping.emplace_back("../outside", bytesOf("x"));
    const auto kEscaping = ContentSource::build(std::move(escaping), kBuild.rootHash, kBuild.publisher.keys);
    RAWFRAME_EXPECT(!kEscaping.has_value() && kEscaping.error().code() == code(ContentError::InvalidLocator));
}

namespace {

/// A Build of one resource, 3, of `text`, in one chunk whose blob is
/// `blob` with codec zstd.
struct ZstdBuild {
    TestPublisher publisher;
    std::filesystem::path root = test::scratchDirectory("content-zstd");
    base::Sha256Digest rootHash{};

    ZstdBuild(std::string_view text, const std::vector<std::byte>& blob) {
        std::filesystem::remove_all(root);
        const std::string kBlob = ContentDigest::of(blob).text();
        const std::string kHex = kBlob.substr(7);
        std::filesystem::create_directories(root / "sha256" / kHex.substr(0, 2));
        std::ofstream{root / "sha256" / kHex.substr(0, 2) / kHex.substr(2), std::ios::binary}.write(
            reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        std::array<char, 32> id{};
        base::formatBits128Hex(idOf(3).value, id);
        std::array<char, 32> type{};
        base::formatBits128Hex(kSoundType.value, type);
        const std::string kContent = ContentDigest::of(bytesOf(text)).text();
        document::Value resource = document::Value::object();
        resource.add("resource", document::Value::string(std::string{id.data(), id.size()}));
        resource.add("type", document::Value::string(std::string{type.data(), type.size()}));
        resource.add("representation", document::Value::string("rawframe.audio.opus"));
        resource.add("digest", document::Value::string(kContent));
        resource.add("size", document::Value::integer(static_cast<std::int64_t>(text.size())));
        document::Value resources = document::Value::array();
        resources.push(std::move(resource));
        document::Value chunk = document::Value::object();
        chunk.add("content", document::Value::string(kContent));
        chunk.add("size", document::Value::integer(static_cast<std::int64_t>(text.size())));
        chunk.add("blob", document::Value::string(kBlob));
        chunk.add("blob_size", document::Value::integer(static_cast<std::int64_t>(blob.size())));
        chunk.add("codec", document::Value::string("zstd"));
        document::Value list = document::Value::array();
        list.push(std::move(chunk));
        document::Value chunks = document::Value::object();
        chunks.add(std::string{id.data(), id.size()}, std::move(list));
        document::Value identity = document::Value::object();
        identity.add("subject", document::Value::string("rawframe/test"));
        identity.add("version", document::Value::string("1.0.0"));
        identity.add("resources", std::move(resources));
        rootHash = base::sha256(*document::writeCanonicalRecord(identity));
        document::Value manifest = document::Value::object();
        manifest.add("schema", document::Value::integer(1));
        manifest.add("identity", std::move(identity));
        manifest.add("chunks", std::move(chunks));
        publisher.publish(root, *document::writeCanonicalRecord(manifest));
    }
    ~ZstdBuild() {
        std::filesystem::remove_all(root);
    }
    ZstdBuild(const ZstdBuild&) = delete;
    ZstdBuild& operator=(const ZstdBuild&) = delete;

    [[nodiscard]] std::pair<std::uint32_t, std::string> read() const {
        auto opened = ContentSource::build(root, rootHash, publisher.keys);
        if (!opened.has_value()) {
            return {opened.error().code().value, "refused"};
        }
        std::vector<BoundManifest> manifests = {BoundManifest{.entries = opened->entries, .source = 0}};
        const std::vector<AdmittedRepresentation> kAdmitted = {
            {.type = kSoundType, .representation = *RepresentationId::parse("rawframe.audio.opus")}};
        Fixture fixture{std::move(opened->source)};
        fixture.store->publish(*ContentCatalog::build(manifests, kAdmitted, 1, 1));
        return outcomeOf(fixture.store->read(ResourceRef{.id = idOf(3), .type = kSoundType}));
    }
};

/// One Zstandard frame of `text`, its content size written or not.
std::vector<std::byte> frameOf(std::string_view text, bool contentSize = true) {
    ZSTD_CCtx* context = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, contentSize ? 1 : 0);
    std::vector<std::byte> frame(ZSTD_compressBound(text.size()));
    const std::size_t kMade = ZSTD_compress2(context, frame.data(), frame.size(), text.data(), text.size());
    ZSTD_freeCCtx(context);
    frame.resize(ZSTD_isError(kMade) != 0U ? 0 : kMade);
    return frame;
}

} // namespace

RAWFRAME_TEST(ZstandardChunksAreBoundedAsSpecified) {
    std::string text;
    while (text.size() < 4096) {
        text += "every shot sounds from where it was fired; ";
    }
    // One frame of the declared size: read as its content.
    RAWFRAME_EXPECT(ZstdBuild(text, frameOf(text)).read() == read(text));
    const auto kInvalid = code(ContentError::ManifestInvalid).value;
    // Bytes after the frame, a second frame, a frame of another size, and one
    // that does not say its size.
    std::vector<std::byte> trailing = frameOf(text);
    trailing.push_back(std::byte{0});
    RAWFRAME_EXPECT(ZstdBuild(text, trailing).read().first == kInvalid);
    std::vector<std::byte> twice = frameOf(text.substr(0, 2048));
    const std::vector<std::byte> kSecond = frameOf(text.substr(2048));
    twice.insert(twice.end(), kSecond.begin(), kSecond.end());
    RAWFRAME_EXPECT(ZstdBuild(text, twice).read().first == kInvalid);
    RAWFRAME_EXPECT(ZstdBuild(text, frameOf(text.substr(1))).read().first == kInvalid);
    RAWFRAME_EXPECT(ZstdBuild(text, frameOf(text, false)).read().first == kInvalid);
    // A skippable frame: its magic, its length, and its payload.
    std::vector<std::byte> skippable = {std::byte{0x50},
                                        std::byte{0x2a},
                                        std::byte{0x4d},
                                        std::byte{0x18},
                                        std::byte{4},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{1},
                                        std::byte{2},
                                        std::byte{3},
                                        std::byte{4}};
    RAWFRAME_EXPECT(ZstdBuild(text, skippable).read().first == kInvalid);
    // Not a frame at all.
    RAWFRAME_EXPECT(ZstdBuild(text, std::vector<std::byte>(64, std::byte{7})).read().first == kInvalid);
}

RAWFRAME_TEST(OnlyAPublishersSignedBuildIsRead) {
    const auto kRefusedAs = [](const result::Result<BuildContent>& opened, signature::SignatureError error) {
        return !opened.has_value() && opened.error().domain() == signature::kSignatureDomain &&
               opened.error().code() == code(error);
    };
    BuildOnDisk build;
    // A byte of the manifest changed after signing.
    {
        std::ifstream file{build.root / "build.manifest", std::ios::binary};
        std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        const std::string kSigned = text;
        text[text.find("1.0.0")] = '2';
        std::ofstream{build.root / "build.manifest", std::ios::binary} << text;
        RAWFRAME_EXPECT(kRefusedAs(ContentSource::build(build.root, build.rootHash, build.publisher.keys),
                                   signature::SignatureError::BadSignature));
        build.publisher.publish(build.root, kSigned);
    }
    RAWFRAME_EXPECT(ContentSource::build(build.root, build.rootHash, build.publisher.keys).has_value());
    // The key revoked, the key set another publisher's, and no signature.
    signature::PublisherKeySet revoked = build.publisher.keys;
    revoked.keys[0].state = signature::KeyState::Revoked;
    RAWFRAME_EXPECT(
        kRefusedAs(ContentSource::build(build.root, build.rootHash, revoked), signature::SignatureError::KeyRevoked));
    signature::PublisherKeySet other = build.publisher.keys;
    other.publisher = "someone";
    RAWFRAME_EXPECT(
        kRefusedAs(ContentSource::build(build.root, build.rootHash, other), signature::SignatureError::UnknownKey));
    std::filesystem::remove(build.root / "build.manifest.sig");
    const auto kUnsigned = ContentSource::build(build.root, build.rootHash, build.publisher.keys);
    RAWFRAME_EXPECT(!kUnsigned.has_value() && kUnsigned.error().code() == code(ContentError::SourceUnavailable));
}
#endif
