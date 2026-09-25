// A game's cooked content in a process: the catalog holds only what the
// process admits, a family's admission publishes the next generation, a
// changed manifest is published and one that does not read is refused with
// the running catalog kept, and a process without content refuses
// admission. A CompositionRecord is its canonical record and nothing else.

#include "rawframe/content/composition_record.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/game_content/cooked_content.h"
#include "rawframe/test/test.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_content;

namespace {

namespace fs = std::filesystem;

constexpr content::ResourceTypeId kSoundType{base::Bits128{.high = 1, .low = 1}};
constexpr content::ResourceTypeId kPictureType{base::Bits128{.high = 2, .low = 2}};

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto kBytes = std::as_bytes(std::span{text.data(), text.size()});
    return {kBytes.begin(), kBytes.end()};
}

content::ResourceId idOf(std::uint64_t id) {
    return content::ResourceId{base::Bits128{.high = 0, .low = id}};
}

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

const content::AdmittedRepresentation kWave{.type = kSoundType,
                                            .representation = *content::RepresentationId::parse("test.wave")};

/// A cook's output of one sound and one picture, and the executor that
/// reads it.
struct Output {
    fs::path root = fs::temp_directory_path() / ("rawframe-game-content-" + std::to_string(::getpid()));
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

RAWFRAME_TEST(AProcessWithoutContentRefusesAdmission) {
    Output output;
    auto opened = output.open(std::nullopt);
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
    // A record of mods opens nothing yet: mod policy does not exist.
    content::CompositionRecord modded = record;
    modded.mods = {{.subject = "fan/hats", .version = "1.0.0", .build = root}};
    const auto kModded = content::writeComposition(modded);
    RAWFRAME_EXPECT(kModded.has_value());
    execution::ManualClock clock;
    execution::CancellationScope scope{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 8}).has_value());
    RAWFRAME_EXPECT(
        !CookedContent::openComposition(io, execution::OwnerId{1}, scope, clock, kModded.value_or(""), "/nowhere")
             .has_value());
    io.stop();
}
