// A game's cooked content in a process: the catalog holds only what the
// process admits, a family's admission publishes the next generation, a
// changed manifest is published and one that does not read is refused with
// the running catalog kept, and a process without content refuses
// admission. A CompositionRecord is its canonical record and nothing else,
// and its Builds open from a library held in memory as from a directory.

#include "rawframe/composition/composition.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/product.h"
#include "rawframe/document/json.h"
#include "rawframe/game_content/cooked_content.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/signature/signature.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"

#include <array>
#include <openssl/evp.h>
#include <string>
#include <vector>

// A cook's output in a directory, where there are files.
#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#include <fstream>
#endif

using namespace rawframe;
using namespace rawframe::game_content;

namespace {

constexpr content::ResourceTypeId kSoundType{base::Bits128{.high = 1, .low = 1}};

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto kBytes = std::as_bytes(std::span{text.data(), text.size()});
    return {kBytes.begin(), kBytes.end()};
}

content::ResourceId idOf(std::uint64_t id) {
    return content::ResourceId{base::Bits128{.high = 0, .low = id}};
}

const content::AdmittedRepresentation kWave{.type = kSoundType,
                                            .representation = *content::RepresentationId::parse("test.wave")};

/// A blocking-I/O executor on the calling thread's terms, and a scope.
struct Reader {
    execution::ManualClock clock;
    execution::CancellationScope scope{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};

    Reader() {
        RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 8}).has_value());
    }
    ~Reader() {
        io.stop();
    }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
};

#if RAWFRAME_FILE_SYSTEM
namespace fs = std::filesystem;

constexpr content::ResourceTypeId kPictureType{base::Bits128{.high = 2, .low = 2}};

content::ManifestEntry entryOf(std::uint64_t id,
                               content::ResourceTypeId type,
                               std::string_view representation,
                               std::string_view locator,
                               std::string_view text) {
    return content::ManifestEntry{.id = idOf(id),
                                  .type = type,
                                  .representation = *content::RepresentationId::parse(representation),
                                  .byteLength = text.size(),
                                  .digest = content::ContentDigest::of(bytesOf(text)),
                                  .locator = std::string{locator}};
}

/// A cook's output of one sound and one picture, and the executor that
/// reads it.
struct Output {
    fs::path root = test::scratchDirectory("game-content");
    execution::ManualClock clock;
    execution::CancellationScope scope{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};

    Output() {
        fs::remove_all(root);
        fs::create_directories(root);
        write("bang", "bang");
        write("bong", "bong");
        write("frame", "frame");
        manifest({entryOf(1, kSoundType, "test.wave", "bang", "bang"),
                  entryOf(2, kPictureType, "test.picture", "frame", "frame")});
        RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 8}).has_value());
    }
    ~Output() {
        io.stop();
        fs::remove_all(root);
    }
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    void write(std::string_view name, std::string_view text) const {
        std::ofstream{root / name, std::ios::binary} << text;
    }
    void manifest(const std::vector<content::ManifestEntry>& entries) const {
        write("content.manifest", content::writeManifest(entries));
    }
    result::Result<std::unique_ptr<CookedContent>> open(std::optional<fs::path> at) {
        return CookedContent::open(io, execution::OwnerId{1}, scope, clock, std::move(at));
    }
};
#endif

/// What resource `id` reads as now, or empty.
std::string readOf(CookedContent& content, std::uint64_t id, content::ResourceTypeId type) {
    auto read = content.store().read(content::ResourceRef{.id = idOf(id), .type = type});
    if (!read.has_value()) {
        return "";
    }
    auto outcome = read->wait();
    if (!outcome.hasValue()) {
        return "";
    }
    const std::span<const std::byte> kBytes = (*outcome).bytes();
    return std::string{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()};
}

} // namespace

#if RAWFRAME_FILE_SYSTEM
RAWFRAME_TEST(TheCatalogHoldsWhatIsAdmitted) {
    Output output;
    {
        auto opened = output.open(output.root);
        RAWFRAME_EXPECT(opened.has_value());
        if (!opened.has_value()) {
            return;
        }
        CookedContent& content = **opened;
        // Nothing admitted: an empty catalog, and a picture the process
        // never admits stays out of every one.
        RAWFRAME_EXPECT(content.generation() == 1 && content.store().catalog()->size() == 0);
        const content::AdmittedRepresentation kTwice[] = {kWave, kWave};
        RAWFRAME_EXPECT(content.admit(kTwice).has_value());
        RAWFRAME_EXPECT(content.generation() == 2 && content.store().catalog()->size() == 1);
        RAWFRAME_EXPECT(readOf(content, 1, kSoundType) == "bang" && readOf(content, 2, kPictureType).empty());
    }
    // A root that is no directory, and one without a manifest, are refused.
    RAWFRAME_EXPECT(!output.open(output.root / "nowhere").has_value());
    fs::remove(output.root / "content.manifest");
    RAWFRAME_EXPECT(!output.open(output.root).has_value());
}

RAWFRAME_TEST(AChangedManifestIsPublishedAndABadOneRefused) {
    Output output;
    auto opened = output.open(output.root);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    CookedContent& content = **opened;
    const content::AdmittedRepresentation kWaves[] = {kWave};
    RAWFRAME_EXPECT(content.admit(kWaves).has_value());
    const auto kUnchanged = content.refresh();
    RAWFRAME_EXPECT(kUnchanged.has_value() && !*kUnchanged && content.generation() == 2);

    // Recooked: the sound's new bytes.
    output.manifest({entryOf(1, kSoundType, "test.wave", "bong", "bong")});
    const auto kChanged = content.refresh();
    RAWFRAME_EXPECT(kChanged.has_value() && *kChanged && content.generation() == 3);
    RAWFRAME_EXPECT(readOf(content, 1, kSoundType) == "bong");

    // A manifest that does not read: refused, the running catalog stays,
    // and it is not read again until it changes again.
    output.write("content.manifest", "{\n  \"kind\": \"content.manifest\"\n}\n");
    const auto kBad = content.refresh();
    RAWFRAME_EXPECT(!kBad.has_value() && content.generation() == 3 && readOf(content, 1, kSoundType) == "bong");
    const auto kAgain = content.refresh();
    RAWFRAME_EXPECT(kAgain.has_value() && !*kAgain);
    // Fixed: published.
    output.manifest({entryOf(1, kSoundType, "test.wave", "bang", "bang")});
    const auto kFixed = content.refresh();
    RAWFRAME_EXPECT(kFixed.has_value() && *kFixed && readOf(content, 1, kSoundType) == "bang");
}

#endif

RAWFRAME_TEST(AProcessWithoutContentRefusesAdmission) {
    Reader reader;
    auto opened = CookedContent::none(reader.io, execution::OwnerId{1}, reader.scope, reader.clock);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    const content::AdmittedRepresentation kWaves[] = {kWave};
    const auto kAdmitted = (*opened)->admit(kWaves);
    RAWFRAME_EXPECT(!kAdmitted.has_value() &&
                    kAdmitted.error().code() == code(content::ContentError::SourceUnavailable));
    RAWFRAME_EXPECT((*opened)->store().catalog() == nullptr && !*(*opened)->refresh());
}

RAWFRAME_TEST(ACompositionRecordIsItsCanonicalRecord) {
    base::Sha256Digest root{};
    root.fill(std::byte{0xab});
    content::CompositionRecord record{.game = {.subject = "rawframe/runners", .version = "0.1.0", .build = root},
                                      .mods = {},
                                      .packages = {{.subject = "rawframe/sounds", .version = "1.0.0", .build = root},
                                                   {.subject = "someone/ui", .version = "2.0.0-rc.1", .build = root}},
                                      .profile = "community",
                                      .createdAt = 1'790'000'000};
    const auto kText = content::writeComposition(record);
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const auto kRead = content::readComposition(*kText);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->game.subject == "rawframe/runners" && kRead->game.build == root &&
                    kRead->packages.size() == 2 && kRead->profile == "community");
    // The identity is the digest of the exact bytes.
    RAWFRAME_EXPECT(content::compositionIdOf(*kText) == base::sha256(*kText));

    // Refused: packages out of subject order, a subject twice, a version
    // outside Semantic Versioning, a subject outside its grammar, a profile
    // too long, and a member added.
    const auto kRefused = [](content::CompositionRecord changed) {
        return !content::writeComposition(changed).has_value();
    };
    content::CompositionRecord swapped = record;
    std::swap(swapped.packages[0], swapped.packages[1]);
    RAWFRAME_EXPECT(kRefused(swapped));
    content::CompositionRecord twice = record;
    twice.packages[1].subject = twice.packages[0].subject;
    RAWFRAME_EXPECT(kRefused(twice));
    content::CompositionRecord version = record;
    version.game.version = "1.0";
    RAWFRAME_EXPECT(kRefused(version));
    content::CompositionRecord subject = record;
    subject.game.subject = "runners";
    RAWFRAME_EXPECT(kRefused(subject));
    content::CompositionRecord profile = record;
    profile.profile = std::string(65, 'a');
    RAWFRAME_EXPECT(kRefused(profile));
    std::string added = *kText;
    added.insert(1, "\"extra\":1,");
    RAWFRAME_EXPECT(!content::readComposition(added).has_value());
    // A record of mods whose Builds the library lacks opens nothing.
    content::CompositionRecord modded = record;
    modded.mods = {{.subject = "fan/hats", .version = "1.0.0", .build = root}};
    const auto kModded = content::writeComposition(modded);
    RAWFRAME_EXPECT(kModded.has_value());
    execution::ManualClock clock;
    execution::CancellationScope scope{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 8}).has_value());
    RAWFRAME_EXPECT(
        !CookedContent::openComposition(io, execution::OwnerId{1}, scope, clock, kModded.value_or(""), HeldLibrary{})
             .has_value());
    io.stop();
}

namespace {

std::string hexOf(base::Bits128 value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return std::string{digits.data(), digits.size()};
}

/// Adds to `library` a Build of `subject` 1.0.0 holding resource `id`,
/// `text`, in one raw chunk, signed with a seed of `seed` and its
/// publisher's key set; returns its root.
base::Sha256Digest
addBuild(HeldLibrary& library, std::string_view subject, std::uint64_t id, std::string_view text, unsigned char seed) {
    const std::vector<std::byte> kBytes = bytesOf(text);
    const std::string kDigest = content::ContentDigest::of(kBytes).text();
    document::Value chunk = document::Value::object();
    chunk.add("content", document::Value::string(kDigest));
    chunk.add("size", document::Value::integer(static_cast<std::int64_t>(text.size())));
    chunk.add("blob", document::Value::string(kDigest));
    chunk.add("blob_size", document::Value::integer(static_cast<std::int64_t>(text.size())));
    chunk.add("codec", document::Value::string("raw"));
    document::Value list = document::Value::array();
    list.push(std::move(chunk));
    document::Value chunks = document::Value::object();
    chunks.add(hexOf(idOf(id).value), std::move(list));
    document::Value resource = document::Value::object();
    resource.add("resource", document::Value::string(hexOf(idOf(id).value)));
    resource.add("type", document::Value::string(hexOf(kSoundType.value)));
    resource.add("representation", document::Value::string("test.wave"));
    resource.add("digest", document::Value::string(kDigest));
    resource.add("size", document::Value::integer(static_cast<std::int64_t>(text.size())));
    document::Value resources = document::Value::array();
    resources.push(std::move(resource));
    document::Value identity = document::Value::object();
    identity.add("subject", document::Value::string(std::string{subject}));
    identity.add("version", document::Value::string("1.0.0"));
    identity.add("resources", std::move(resources));
    const base::Sha256Digest kRoot = base::sha256(*document::writeCanonicalRecord(identity));
    document::Value manifest = document::Value::object();
    manifest.add("schema", document::Value::integer(1));
    manifest.add("identity", std::move(identity));
    manifest.add("chunks", std::move(chunks));
    const std::string kManifest = *document::writeCanonicalRecord(manifest);

    std::array<unsigned char, 32> seedBytes{};
    seedBytes.fill(seed);
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seedBytes.data(), seedBytes.size());
    signature::PublicKey publicKey{};
    std::size_t length = publicKey.size();
    EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char*>(publicKey.data()), &length);
    signature::Envelope envelope{.kid = "0000000000000001", .sig = {}};
    length = envelope.sig.size();
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key);
    EVP_DigestSign(context,
                   reinterpret_cast<unsigned char*>(envelope.sig.data()),
                   &length,
                   reinterpret_cast<const unsigned char*>(kManifest.data()),
                   kManifest.size());
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    const std::string kPublisher{content::publisherOf(subject)};
    const signature::PublisherKeySet kKeys{
        .publisher = kPublisher,
        .sequence = 1,
        .updatedAt = 1,
        .head = "sha256:" + std::string(64, '0'),
        .keys = {signature::PublisherKey{
            .kid = "0000000000000001", .publicKey = publicKey, .state = signature::KeyState::Active, .since = 1}}};

    const std::string kBuild = "builds/" + content::ContentDigest{.bytes = kRoot}.text().substr(7) + "/";
    library.emplace_back(kBuild + "build.manifest", bytesOf(kManifest));
    library.emplace_back(kBuild + "build.manifest.sig", bytesOf(signature::writeEnvelope(envelope)));
    library.emplace_back(kBuild + "sha256/" + kDigest.substr(7, 2) + "/" + kDigest.substr(9), kBytes);
    library.emplace_back("keys/" + kPublisher + ".keys", bytesOf(*signature::writePublisherKeySet(kKeys)));
    return kRoot;
}

/// A Game Build of `rawframe/test` 1.0.0 holding resource 1, "bang", and
/// the Composition that names it: a library a web client could have
/// fetched, never on disk.
struct HeldGame {
    HeldLibrary library;
    std::string record;

    HeldGame() {
        const base::Sha256Digest kRoot = addBuild(library, "rawframe/test", 1, "bang", 0x42);
        record = *content::writeComposition(
            content::CompositionRecord{.game = {.subject = "rawframe/test", .version = "1.0.0", .build = kRoot},
                                       .mods = {},
                                       .packages = {},
                                       .profile = "community",
                                       .createdAt = 1'790'000'000});
    }
};

} // namespace

RAWFRAME_TEST(ACompositionOpensFromALibraryHeldInMemory) {
    Reader reader;
    const HeldGame kGame;
    auto opened = CookedContent::openComposition(
        reader.io, execution::OwnerId{1}, reader.scope, reader.clock, kGame.record, kGame.library);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    const content::AdmittedRepresentation kWaves[] = {kWave};
    RAWFRAME_EXPECT((*opened)->held() && (*opened)->admit(kWaves).has_value());
    RAWFRAME_EXPECT(readOf(**opened, 1, kSoundType) == "bang");
    RAWFRAME_EXPECT((*opened)->compositionId() == content::compositionIdOf(kGame.record) && !*(*opened)->refresh());

    // Without the publisher's key set, or with the Build's blob changed,
    // it is refused or its bytes are.
    HeldLibrary keyless = kGame.library;
    keyless.pop_back();
    RAWFRAME_EXPECT(!CookedContent::openComposition(
                         reader.io, execution::OwnerId{1}, reader.scope, reader.clock, kGame.record, std::move(keyless))
                         .has_value());
    HeldLibrary changed = kGame.library;
    changed[2].second = bytesOf("bong");
    auto tampered = CookedContent::openComposition(
        reader.io, execution::OwnerId{1}, reader.scope, reader.clock, kGame.record, std::move(changed));
    RAWFRAME_EXPECT(tampered.has_value() && (*tampered)->admit(kWaves).has_value() &&
                    readOf(**tampered, 1, kSoundType).empty());
}

RAWFRAME_TEST(ACompositionsModsAreReadAfterItsGame) {
    Reader reader;
    HeldLibrary library;
    const base::Sha256Digest kGame = addBuild(library, "rawframe/test", 1, "bang", 0x42);
    const base::Sha256Digest kHats = addBuild(library, "fan/hats", 2, "hats", 0x43);
    const base::Sha256Digest kCapes = addBuild(library, "other/capes", 3, "capes", 0x44);
    content::CompositionRecord modded{.game = {.subject = "rawframe/test", .version = "1.0.0", .build = kGame},
                                      .mods = {{.subject = "fan/hats", .version = "1.0.0", .build = kHats},
                                               {.subject = "other/capes", .version = "1.0.0", .build = kCapes}},
                                      .packages = {},
                                      .profile = "community",
                                      .createdAt = 1'790'000'000};
    auto opened = CookedContent::openComposition(reader.io,
                                                 execution::OwnerId{1},
                                                 reader.scope,
                                                 reader.clock,
                                                 content::writeComposition(modded).value_or(""),
                                                 library);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    // The game and each mod as its record names it, with what it holds; the
    // mods' resources are in the one catalog.
    const ComposedBuild* game = (*opened)->composedGame();
    const std::span<const ComposedBuild> kMods = (*opened)->composedMods();
    RAWFRAME_EXPECT(game != nullptr && game->reference.subject == "rawframe/test" && game->entries.size() == 1);
    RAWFRAME_EXPECT(kMods.size() == 2 && kMods[0].reference.subject == "fan/hats" && kMods[0].entries.size() == 1 &&
                    kMods[0].entries[0].id == idOf(2) && kMods[1].reference.build == kCapes);
    const content::AdmittedRepresentation kWaves[] = {kWave};
    RAWFRAME_EXPECT((*opened)->admit(kWaves).has_value() && readOf(**opened, 2, kSoundType) == "hats" &&
                    readOf(**opened, 1, kSoundType) == "bang");
    // A mod whose Build is another subject's is refused, as a package is.
    modded.mods[1].subject = "other/cloaks";
    RAWFRAME_EXPECT(!CookedContent::openComposition(reader.io,
                                                    execution::OwnerId{1},
                                                    reader.scope,
                                                    reader.clock,
                                                    content::writeComposition(modded).value_or(""),
                                                    library)
                         .has_value());
    // Content that is not a Composition has none.
    auto none = CookedContent::none(reader.io, execution::OwnerId{1}, reader.scope, reader.clock);
    RAWFRAME_EXPECT(none.has_value() && (*none)->composedGame() == nullptr && (*none)->composedMods().empty());
}

namespace {

/// The content the Runtime was composed with, found through a participant
/// of the test's own.
GameContent* composed = nullptr;

constexpr std::string_view kNeedsContent[] = {kGameContent.name};

void registerFinder(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "test.finder",
        .factory =
            [](composition::ParticipantContext& context) noexcept -> result::Result<composition::ParticipantOwner> {
            RAWFRAME_TRY_ASSIGN(composed, context.capability(kGameContent));
            struct Finder final : composition::Participant {};
            return composition::ParticipantOwner{new Finder{}};
        },
        .scope = composition::LifetimeScope::Runtime,
        .requiredCapabilities = kNeedsContent,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
    });
}

} // namespace

RAWFRAME_TEST(AHostHoldsItsCompositionAsItFetchedIt) {
    // The record and the library among the files the host holds (D167),
    // named by the configuration as a disk's would be.
    const HeldGame kGame;
    std::vector<composition::HeldFiles::File> files;
    for (const auto& [path, bytes] : kGame.library) {
        files.emplace_back("library/" + path, bytes);
    }
    files.emplace_back("game.composition", bytesOf(kGame.record));
    const auto kHeld = composition::HeldFiles::of(std::move(files));
    RAWFRAME_EXPECT(kHeld.has_value());
    const std::array<composition::RegistrarEntry, 2> kRegistrars = {
        composition::RegistrarEntry{"game_content", &registerParticipants, kScopes},
        composition::RegistrarEntry{"test", &registerFinder, kScopes}};
    std::vector<composition::Problem> problems;
    const auto kPlan = composition::compose(
        composition::CompositionRequest{.registrars = kRegistrars,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    RAWFRAME_EXPECT(kPlan.has_value());
    if (!kPlan.has_value() || !kHeld.has_value()) {
        return;
    }
    Reader reader;
    const auto kStart = [&](std::string_view text) {
        const auto kConfiguration = composition::Configuration::parse(text);
        composition::Composition composition{*kPlan,
                                             composition::HostServices{.clock = &reader.clock,
                                                                       .scope = &reader.scope,
                                                                       .blockingIo = &reader.io,
                                                                       .configuration = &*kConfiguration,
                                                                       .files = &*kHeld}};
        composed = nullptr;
        const bool kStarted = composition.start().has_value();
        const bool kRead =
            kStarted && composed != nullptr && composed->compositionId() == content::compositionIdOf(kGame.record);
        composition.stop();
        return kRead;
    };
    RAWFRAME_EXPECT(kStart("content.composition = game.composition\ncontent.library = library\n"));
    // Not held, without a library, or a cook's output: refused.
    RAWFRAME_EXPECT(!kStart("content.composition = other.composition\ncontent.library = library\n"));
    RAWFRAME_EXPECT(!kStart("content.composition = game.composition\n"));
    RAWFRAME_EXPECT(!kStart("content.root = library\n"));
}
