// Cooking against ADR-0024: sidecar identity, content-addressed artifacts a
// content store reads back verified, a cache keyed on every input (the tool
// included) that is verified before it is trusted, double cooks that catch
// a nondeterministic importer, and nothing published unless nothing failed.

#include "rawframe/audio/decode.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/store.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/errors.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using namespace rawframe::cook;

namespace {

namespace fs = std::filesystem;

const std::string kToneId = "000000000000000000000000000000a1";
const std::string kMp3Id = "000000000000000000000000000000a2";

std::string readText(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

void writeText(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

std::string sidecar(std::string_view id, std::string_view settings = "", std::string_view importer = "rawframe.audio") {
    std::string text = "{\n  \"schema\": 1,\n  \"resourceId\": \"" + std::string{id} + "\",\n  \"importer\": \"" +
                       std::string{importer} + "\"";
    if (!settings.empty()) {
        text += ",\n  \"settings\": {\n    " + std::string{settings} + "\n  }";
    }
    return text + "\n}\n";
}

/// A project of two sources: a Vorbis tone cooked to Opus, and an MP3 tone
/// in the short-form tier.
struct Project {
    fs::path base = fs::temp_directory_path() / ("rawframe-cook-" + std::to_string(::getpid()));
    fs::path sources = base / "sources";
    fs::path output = base / "output";
    fs::path cache = base / "cache";

    Project() {
        fs::remove_all(base);
        fs::create_directories(sources / "sounds");
        fs::copy_file(fs::path{RAWFRAME_COOK_DATA} / "tone.ogg", sources / "sounds" / "tone.ogg");
        fs::copy_file(fs::path{RAWFRAME_COOK_DATA} / "tone.mp3", sources / "sounds" / "tone.mp3");
        writeText(sources / "sounds" / "tone.ogg.rfmeta", sidecar(kToneId, "\"tier\": \"opus\""));
        writeText(sources / "sounds" / "tone.mp3.rfmeta", sidecar(kMp3Id));
    }
    ~Project() {
        fs::remove_all(base);
    }
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    CookReport cook(std::uint8_t tool = 1, std::optional<fs::path> into = std::nullopt) const {
        static const std::array<Importer, 1> kImporters = {audioImporter()};
        base::Sha256Digest toolchain{};
        toolchain[0] = std::byte{tool};
        auto report = cookSources(CookRequest{.sources = sources,
                                              .output = into.value_or(output),
                                              .cache = cache,
                                              .importers = kImporters,
                                              .toolchain = toolchain,
                                              .target = "any"});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    }
};

bool failedWith(const CookReport& report, CookError error) {
    return std::ranges::any_of(report.failures, [error](const result::Error& each) {
        return each.domain() == kCookDomain && each.code() == code(error);
    });
}

} // namespace

RAWFRAME_TEST(SourcesCookIntoVerifiedArtifacts) {
    const Project kProject;
    const CookReport kFirst = kProject.cook();
    RAWFRAME_EXPECT(kFirst.cooked == 2 && kFirst.reused == 0 && kFirst.failures.empty());
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value() && kManifest->size() == 2);
    if (!kManifest.has_value() || kManifest->size() != 2) {
        return;
    }
    // In resource order: the Opus tone, then the WAVE tone; each object's
    // bytes are exactly what the manifest says.
    const content::ManifestEntry& opus = (*kManifest)[0];
    const content::ManifestEntry& wave = (*kManifest)[1];
    RAWFRAME_EXPECT(opus.representation.text() == "rawframe.audio.opus" && opus.type == kSoundClipType);
    RAWFRAME_EXPECT(wave.representation.text() == "rawframe.audio.wave" && wave.type == kSoundClipType);
    for (const content::ManifestEntry& each : *kManifest) {
        const std::string kBytes = readText(kProject.output / each.locator);
        RAWFRAME_EXPECT(kBytes.size() == each.byteLength &&
                        content::ContentDigest::of(std::as_bytes(std::span{kBytes.data(), kBytes.size()})) ==
                            each.digest &&
                        each.locator == "objects/" + each.digest.text().substr(7));
    }
    const std::string kReceipt = readText(kProject.output / "cook.receipt");
    RAWFRAME_EXPECT(kReceipt.find("\"failures\": 0") != std::string::npos &&
                    kReceipt.find("\"determinism\": \"double_cook\"") != std::string::npos);
    // The receipt names the manifest beside it by digest.
    const std::string kManifestRead = readText(kProject.output / "content.manifest");
    const std::string kNamed =
        "\"manifest\": \"" +
        content::ContentDigest::of(std::as_bytes(std::span{kManifestRead.data(), kManifestRead.size()})).text() + "\"";
    RAWFRAME_EXPECT(kReceipt.find(kNamed) != std::string::npos);

    // Again: everything from the cache, and the same manifest byte for byte.
    const std::string kManifestText = readText(kProject.output / "content.manifest");
    const CookReport kAgain = kProject.cook();
    RAWFRAME_EXPECT(kAgain.cooked == 0 && kAgain.reused == 2 && kAgain.failures.empty());
    RAWFRAME_EXPECT(readText(kProject.output / "content.manifest") == kManifestText);
    // Another tool reuses nothing another made.
    RAWFRAME_EXPECT(kProject.cook(2).cooked == 2);
}

RAWFRAME_TEST(TheCacheIsKeyedOnEveryInputAndVerified) {
    const Project kProject;
    static_cast<void>(kProject.cook());
    // A setting changed: that source again, the other reused.
    writeText(kProject.sources / "sounds" / "tone.ogg.rfmeta",
              sidecar(kToneId, "\"tier\": \"opus\",\n    \"bitrate\": 32000"));
    const CookReport kChanged = kProject.cook();
    RAWFRAME_EXPECT(kChanged.cooked == 1 && kChanged.reused == 1);
    // A source changed: that one again.
    fs::copy_file(fs::path{RAWFRAME_COOK_DATA} / "tone.ogg",
                  kProject.sources / "sounds" / "tone.mp3",
                  fs::copy_options::overwrite_existing);
    const CookReport kEdited = kProject.cook();
    RAWFRAME_EXPECT(kEdited.cooked == 1 && kEdited.reused == 1);
    // Every cached object corrupted: none is trusted, all are cooked anew.
    for (const auto& object : fs::directory_iterator{kProject.cache / "objects"}) {
        writeText(object.path(), "corrupt");
    }
    const CookReport kCorrupt = kProject.cook();
    RAWFRAME_EXPECT(kCorrupt.cooked == 2 && kCorrupt.reused == 0 && kCorrupt.failures.empty());
}

RAWFRAME_TEST(NothingIsPublishedUnlessNothingFailed) {
    const Project kProject;
    writeText(kProject.sources / "a.wav.rfmeta", sidecar("000000000000000000000000000000b1"));
    writeText(kProject.sources / "b.png.rfmeta", sidecar("000000000000000000000000000000b2", "", "rawframe.texture"));
    writeText(kProject.sources / "c.ogg.rfmeta", sidecar(kToneId));
    fs::copy_file(fs::path{RAWFRAME_COOK_DATA} / "tone.ogg", kProject.sources / "c.ogg");
    writeText(kProject.sources / "d.ogg.rfmeta", sidecar("000000000000000000000000000000b4", "\"tier\": \"flac\""));
    writeText(kProject.sources / "e.ogg.rfmeta", "{\n  \"schema\": 2\n}\n");
    const CookReport kReport = kProject.cook();
    RAWFRAME_EXPECT(failedWith(kReport, CookError::MissingSource) && failedWith(kReport, CookError::UnknownImporter) &&
                    failedWith(kReport, CookError::DuplicateResource) && failedWith(kReport, CookError::BadSidecar));
    RAWFRAME_EXPECT(!fs::exists(kProject.output / "content.manifest") && !fs::exists(kProject.output / "cook.receipt"));

    // An output inside the sources is refused outright.
    static const std::array<Importer, 1> kImporters = {audioImporter()};
    const auto kInside = cookSources(
        CookRequest{.sources = kProject.sources, .output = kProject.sources / "out", .importers = kImporters});
    RAWFRAME_EXPECT(!kInside.has_value() && kInside.error().code() == code(CookError::BadRequest));
}

RAWFRAME_TEST(ANondeterministicImporterIsCaught) {
    const Project kProject;
    static std::atomic<int> calls{0};
    static const std::array<Importer, 1> kWobbly = {
        Importer{.identity = "rawframe.audio",
                 .normalize = [](const document::Value*) -> result::Result<std::string> {
                     return std::string{"x"};
                 },
                 .cook = [](std::span<const std::byte>, std::string_view) -> result::Result<Artifact> {
                     return Artifact{.type = kSoundClipType,
                                     .representation = *content::RepresentationId::parse("rawframe.audio.wave"),
                                     .bytes = {static_cast<std::byte>(calls.fetch_add(1))}};
                 }}};
    const auto kReport = cookSources(
        CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kWobbly, .toolchain = {}});
    RAWFRAME_EXPECT(kReport.has_value() && failedWith(*kReport, CookError::Nondeterministic) &&
                    !fs::exists(kProject.output / "cook.receipt"));
}

RAWFRAME_TEST(ACookedSoundIsReadByIdentityAndPlays) {
    // End to end: the cook's output is a content source, its manifest the
    // catalog, and a sound read by its identity decodes to its frames.
    const Project kProject;
    static_cast<void>(kProject.cook());
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{3}, {.maximumPendingTasks = 8}).has_value());
    {
        std::vector<content::ContentSource> sources;
        sources.push_back(std::move(*content::ContentSource::directory(kProject.output)));
        auto store =
            std::move(*content::ContentStore::create(io, execution::OwnerId{3}, root, clock, std::move(sources)));
        const std::vector<content::AdmittedRepresentation> kAdmitted = {
            {.type = kSoundClipType, .representation = *content::RepresentationId::parse("rawframe.audio.opus")},
            {.type = kSoundClipType, .representation = *content::RepresentationId::parse("rawframe.audio.wave")}};
        const std::vector<content::BoundManifest> kManifests = {content::BoundManifest{
            .entries = *content::readManifest(readText(kProject.output / "content.manifest")), .source = 0}};
        auto catalog = content::ContentCatalog::build(kManifests, kAdmitted, 1, 1);
        RAWFRAME_EXPECT(catalog.has_value());
        store->publish(std::move(*catalog));
        auto read = store->read(content::ResourceRef{.id = content::ResourceId{base::parseBits128Hex(kToneId).value},
                                                     .type = kSoundClipType});
        RAWFRAME_EXPECT(read.has_value());
        if (read.has_value()) {
            auto outcome = read->wait();
            RAWFRAME_EXPECT(outcome.hasValue());
            if (outcome.hasValue()) {
                const auto kClip = audio::decodeCooked((*outcome).bytes());
                RAWFRAME_EXPECT(kClip.has_value() && kClip->frames() == 12'000 && kClip->channels == 2);
            }
        }
    }
    io.stop();
}
