// Cooking against ADR-0024: sidecar identity, content-addressed artifacts a
// content store reads back verified, a cache keyed on every input (the tool
// included) that is verified before it is trusted, double cooks that catch
// a nondeterministic importer, and nothing published unless nothing failed.

#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/mask.h"
#include "rawframe/animation/resources.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/audio/decode.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/store.h"
#include "rawframe/cook/animation.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/errors.h"
#include "rawframe/cook/game.h"
#include "rawframe/cook/kest.h"
#include "rawframe/cook/mesh.h"
#include "rawframe/cook/scene.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/mesh/errors.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/cooked_game.h"

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

namespace {

/// An importer whose source names a directory: it gathers every `.part`
/// file under it, each read through the cook, into one artifact.
result::Result<Artifact> gather(std::span<const std::byte> source, std::string_view, Reads& reads) {
    const std::string kDirectory{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kNames, reads.files(kDirectory, ".part"));
    std::vector<std::byte> bytes;
    for (const std::string& name : kNames) {
        RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kPart, reads.file(kDirectory + "/" + name));
        bytes.insert(bytes.end(), kPart.begin(), kPart.end());
    }
    return Artifact{.type = kSoundClipType,
                    .representation = *content::RepresentationId::parse("rawframe.audio.wave"),
                    .bytes = std::move(bytes)};
}

CookReport cookGathered(const Project& project) {
    static const std::array<Importer, 2> kImporters = {
        audioImporter(),
        Importer{.identity = "test.gather",
                 .normalize = [](const document::Value*) -> result::Result<std::string> {
                     return std::string{};
                 },
                 .cook = &gather}};
    auto report = cookSources(CookRequest{
        .sources = project.sources, .output = project.output, .cache = project.cache, .importers = kImporters});
    RAWFRAME_EXPECT(report.has_value());
    return report.has_value() ? std::move(*report) : CookReport{};
}

} // namespace

RAWFRAME_TEST(WhatAnImporterReadsIsAnInputToo) {
    const Project kProject;
    writeText(kProject.sources / "bundle" / "bundle.txt", "parts");
    writeText(kProject.sources / "bundle" / "bundle.txt.rfmeta",
              sidecar("000000000000000000000000000000a3", "", "test.gather"));
    writeText(kProject.sources / "bundle" / "parts" / "a.part", "one");
    writeText(kProject.sources / "bundle" / "parts" / "b.part", "two");
    const CookReport kFirst = cookGathered(kProject);
    RAWFRAME_EXPECT(kFirst.cooked == 3 && kFirst.failures.empty());
    // The receipt names each read: the listing and both files.
    const std::string kReceipt = readText(kProject.output / "cook.receipt");
    RAWFRAME_EXPECT(kReceipt.find("\"path\": \"bundle/parts/a.part\"") != std::string::npos &&
                    kReceipt.find("\"path\": \"bundle/parts/b.part\"") != std::string::npos &&
                    kReceipt.find("\"suffix\": \".part\"") != std::string::npos);
    RAWFRAME_EXPECT(cookGathered(kProject).reused == 3);

    // A part read changed: the bundle again, the sounds reused.
    writeText(kProject.sources / "bundle" / "parts" / "a.part", "uno");
    const CookReport kEdited = cookGathered(kProject);
    RAWFRAME_EXPECT(kEdited.cooked == 1 && kEdited.reused == 2);
    // A part added deeper changes the listing: again.
    writeText(kProject.sources / "bundle" / "parts" / "more" / "c.part", "three");
    RAWFRAME_EXPECT(cookGathered(kProject).cooked == 1);
    // A file the listing does not take changes nothing.
    writeText(kProject.sources / "bundle" / "parts" / "notes.txt", "unread");
    RAWFRAME_EXPECT(cookGathered(kProject).reused == 3);
    // A part removed: again, from what is there.
    fs::remove(kProject.sources / "bundle" / "parts" / "b.part");
    RAWFRAME_EXPECT(cookGathered(kProject).cooked == 1);

    // Nothing outside the sources is read, by `..` or from the root.
    for (const std::string_view kOutside : {"../..", "/tmp", ""}) {
        writeText(kProject.sources / "bundle" / "bundle.txt", kOutside);
        const CookReport kRefused = cookGathered(kProject);
        RAWFRAME_EXPECT(failedWith(kRefused, CookError::BadRead));
    }
}

RAWFRAME_TEST(AKestProjectCooksIntoItsFiles) {
    const Project kProject;
    const fs::path kGame = kProject.sources / "game";
    writeText(kGame / "kest.project", "project shots\nsource .\nsource ../../modules/kest_library/kest\n");
    writeText(kGame / "kest.project.rfmeta", sidecar("000000000000000000000000000000a4", "", "rawframe.kest"));
    writeText(kGame / "shots.kest", "module shots\n\nimport rawframe.world\nimport rules.score\n\nfn fire() {\n}\n");
    writeText(kGame / "rules" / "score.kest", "module rules.score\n\nfn add() {\n}\n");
    writeText(kGame / "notes.txt", "not Kest");
    static const std::array<Importer, 2> kImporters = {audioImporter(), kestImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    RAWFRAME_EXPECT(kCook().cooked == 3);
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    if (!kManifest.has_value()) {
        return;
    }
    const auto kEntry = std::ranges::find(
        *kManifest, content::ResourceTypeId{kest_library::kGameSourcesType}, &content::ManifestEntry::type);
    RAWFRAME_EXPECT(kEntry != kManifest->end());
    if (kEntry == kManifest->end()) {
        return;
    }
    // Its own two files, the notes and the library left out, and they
    // compile with the engine's library alone.
    const auto kFiles = kest_library::readGameSources(readText(kProject.output / kEntry->locator));
    RAWFRAME_EXPECT(kFiles.has_value() && kFiles->size() == 2 && (*kFiles)[0].path == "rules/score.kest" &&
                    (*kFiles)[1].path == "shots.kest");
    RAWFRAME_EXPECT(kFiles.has_value() && kest_library::compile("shots.kest", *kFiles, {}).has_value());

    // A file changed or added: the project again.
    writeText(kGame / "rules" / "score.kest", "module rules.score\n\nfn add() {\n}\n\nfn take() {\n}\n");
    RAWFRAME_EXPECT(kCook().cooked == 1);
    writeText(kGame / "more.kest", "module more\n");
    RAWFRAME_EXPECT(kCook().cooked == 1);
    RAWFRAME_EXPECT(kCook().reused == 3);
    // Settings are refused.
    writeText(kGame / "kest.project.rfmeta",
              sidecar("000000000000000000000000000000a4", "\"entry\": \"shots.kest\"", "rawframe.kest"));
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadSidecar));
}

RAWFRAME_TEST(AGameCooksWithEverythingItNames) {
    const Project kProject;
    const fs::path kGame = kProject.sources / "runners";
    fs::copy(fs::path{RAWFRAME_SAMPLE_GAMES} / "runners", kGame, fs::copy_options::recursive);
    const std::string kSourcesId = "f9f0181057571ecd398d86d2c34a641f";
    writeText(kGame / "runners.game.rfmeta", sidecar("000000000000000000000000000000a5", "", "rawframe.game"));
    static const std::array<Importer, 4> kImporters = {
        audioImporter(), gameImporter(), kestImporter(), sceneImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    const CookReport kFirst = kCook();
    RAWFRAME_EXPECT(kFirst.cooked == 8 && kFirst.failures.empty());
    const auto kCooked = [&kProject]() -> std::optional<world_kest::CookedGame> {
        const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
        if (!kManifest.has_value()) {
            return std::nullopt;
        }
        const auto kEntry = std::ranges::find(
            *kManifest, content::ResourceTypeId{world_kest::kCookedGameType}, &content::ManifestEntry::type);
        if (kEntry == kManifest->end()) {
            return std::nullopt;
        }
        auto read = world_kest::readCookedGame(readText(kProject.output / kEntry->locator));
        return read.has_value() ? std::optional{std::move(*read)} : std::nullopt;
    };
    const auto kGameRead = kCooked();
    RAWFRAME_EXPECT(kGameRead.has_value());
    if (!kGameRead.has_value()) {
        return;
    }
    // The text as written; the three documents it names; its two scenes by
    // resource; both programs as
    // entries of the project's sources.
    RAWFRAME_EXPECT(kGameRead->text == readText(kGame / "runners.game"));
    RAWFRAME_EXPECT(kGameRead->files.size() == 3 && kGameRead->file("runners.actions") != nullptr &&
                    kGameRead->scenes.size() == 2 && kGameRead->scene("level.scene") != nullptr &&
                    kGameRead->scene("hall.scene") != nullptr &&
                    kGameRead->scene("level.scene")->scene ==
                        base::parseBits128Hex("52771075251e7361deaecf4939c72e56").value &&
                    kGameRead->file("runners.mixer") != nullptr && kGameRead->file("shot.sound") != nullptr &&
                    kGameRead->file("shot.sound")->text == readText(kGame / "shot.sound"));
    const base::Bits128 kSources = base::parseBits128Hex(kSourcesId).value;
    RAWFRAME_EXPECT(kGameRead->programs.size() == 2 && kGameRead->program("runners.kest") != nullptr &&
                    kGameRead->program("runners.kest")->sources == kSources &&
                    kGameRead->program("sample.kest") != nullptr &&
                    kGameRead->program("sample.kest")->entry == "sample.kest");

    // A document it names changed: the description again, and the change
    // is in it.
    std::string sound = readText(kGame / "shot.sound");
    sound.replace(sound.find("\"maximumDistance\": 30"), 21, "\"maximumDistance\": 40");
    writeText(kGame / "shot.sound", sound);
    RAWFRAME_EXPECT(kCook().cooked == 1);
    RAWFRAME_EXPECT(kCooked().has_value() && kCooked()->file("shot.sound")->text == sound);

    // What it names must do: a program that compiles, a mixer that reads.
    const std::string kProgram = readText(kGame / "runners.kest");
    writeText(kGame / "runners.kest", kProgram + "\nfn broken( {\n");
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadReference));
    writeText(kGame / "runners.kest", kProgram);
    const std::string kMixer = readText(kGame / "runners.mixer");
    writeText(kGame / "runners.mixer", "{}\n");
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadReference));
    writeText(kGame / "runners.mixer", kMixer);
    // A sound on a bus the mixer does not have.
    std::string elsewhere = sound;
    elsewhere.replace(elsewhere.find("691c5abeb88d55ee"), 16, "0000000000000bad");
    writeText(kGame / "shot.sound", elsewhere);
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadReference));
    writeText(kGame / "shot.sound", sound);
    // A project without its sidecar names no sources.
    fs::rename(kGame / "kest.project.rfmeta", kProject.base / "aside");
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadReference));
    fs::rename(kProject.base / "aside", kGame / "kest.project.rfmeta");
    RAWFRAME_EXPECT(kCook().failures.empty());

    // A scene it names is named by its resource; one without a sidecar is
    // refused, and one that does not read is its importer's failure.
    writeText(kGame / "runners.game", readText(kGame / "runners.game") + "scene extra.scene\n");
    writeText(
        kGame / "extra.scene",
        "{\n  \"kind\": \"rawframe.scene\",\n  \"formatVersion\": 1,\n  \"schema\": {},\n  \"entities\": []\n}\n");
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadReference));
    writeText(kGame / "extra.scene.rfmeta", sidecar("000000000000000000000000000000a6", "", "rawframe.scene"));
    RAWFRAME_EXPECT(kCook().failures.empty() && kCooked().has_value() && kCooked()->scene("extra.scene") != nullptr);
    writeText(kGame / "extra.scene", "{}\n");
    RAWFRAME_EXPECT(!kCook().failures.empty());
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

RAWFRAME_TEST(AGltfCooksIntoAMeshWithItsBuffers) {
    const Project kProject;
    const fs::path kProps = kProject.sources / "props";
    std::string buffer;
    for (const float kValue : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F}) {
        buffer.append(reinterpret_cast<const char*>(&kValue), sizeof(kValue));
    }
    writeText(kProps / "shard.bin", buffer);
    writeText(kProps / "shard.gltf",
              R"({"asset": {"version": "2.0"}, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}], )"
              R"("meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}], )"
              R"("buffers": [{"uri": "shard.bin", "byteLength": 36}], )"
              R"("bufferViews": [{"buffer": 0, "byteLength": 36}], )"
              R"("accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", )"
              R"("min": [0, 0, 0], "max": [1, 1, 0]}]})");
    writeText(kProps / "shard.gltf.rfmeta", sidecar("000000000000000000000000000000a7", "", "rawframe.mesh"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), meshImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    const auto kMeshOf = [&kProject]() -> std::optional<mesh::Mesh> {
        const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
        if (!kManifest.has_value()) {
            return std::nullopt;
        }
        for (const content::ManifestEntry& each : *kManifest) {
            if (each.type.value == mesh::kMeshType && each.representation.text() == mesh::kMeshRepresentation) {
                const std::string kBytes = readText(kProject.output / each.locator);
                auto read = mesh::decode(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
                return read.has_value() ? std::optional{std::move(*read)} : std::nullopt;
            }
        }
        return std::nullopt;
    };
    RAWFRAME_EXPECT(kCook().cooked == 3);
    const auto kFirst = kMeshOf();
    RAWFRAME_EXPECT(kFirst.has_value() && kFirst->indices == (std::vector<std::uint32_t>{0, 1, 2}) &&
                    kFirst->positions[1] == (mesh::Vector3{1.0F, 0.0F, 0.0F}));
    // The buffer is an input: changed, the mesh cooks again from it.
    const float kTwo = 2.0F;
    buffer.replace(12, sizeof(kTwo), reinterpret_cast<const char*>(&kTwo), sizeof(kTwo));
    writeText(kProps / "shard.bin", buffer);
    const CookReport kEdited = kCook();
    RAWFRAME_EXPECT(kEdited.cooked == 1 && kEdited.reused == 2);
    const auto kSecond = kMeshOf();
    RAWFRAME_EXPECT(kSecond.has_value() && kSecond->positions[1] == (mesh::Vector3{2.0F, 0.0F, 0.0F}));
    // A buffer gone, or named outside the sources, fails the mesh.
    fs::rename(kProps / "shard.bin", kProject.sources / "shard.bin");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    std::string gltf = readText(kProps / "shard.gltf");
    gltf.replace(gltf.find("shard.bin"), 9, "../../shard.bin");
    writeText(kProps / "shard.gltf", gltf);
    const CookReport kOutside = kCook();
    RAWFRAME_EXPECT(kOutside.failures.size() == 1 && kOutside.failures[0].domain() == mesh::kMeshDomain);
}

RAWFRAME_TEST(AnimationDocumentsCookIntoResourcesOfTheirKind) {
    const Project kProject;
    const fs::path kRig = kProject.sources / "rig";
    const base::Bits128 kSkeletonId{0, 0xb1};
    const animation::Skeleton kSkeleton{
        .bones = {animation::Bone{.target = {1, 1}, .name = "root", .parent = std::nullopt, .bind = {}}}};
    const animation::Clip kClip{.skeleton = kSkeletonId,
                                .duration = 1.0,
                                .loop = animation::Loop::Loop,
                                .tracks = {animation::Track{.bone = {1, 1}, .keys = {animation::Key{}}}}};
    const animation::Graph kGraph{
        .parameters = {},
        .nodes = {animation::GraphNode{.id = 1, .node = animation::ClipNode{.clip = {0, 0xb2}}},
                  animation::GraphNode{.id = 2, .node = animation::OutputNode{.pose = {.node = 1}}}},
        .presentation = {}};
    const std::string kSkeletonText = *animation::writeSkeleton(kSkeleton);
    writeText(kRig / "body.rfanim", kSkeletonText);
    writeText(kRig / "walk.rfanim", *animation::writeClip(kClip));
    writeText(kRig / "moves.rfanim", *animation::writeGraph(kGraph));
    writeText(kRig / "body.rfanim.rfmeta", sidecar("000000000000000000000000000000b1", "", "rawframe.animation"));
    writeText(kRig / "walk.rfanim.rfmeta", sidecar("000000000000000000000000000000b2", "", "rawframe.animation"));
    writeText(kRig / "moves.rfanim.rfmeta", sidecar("000000000000000000000000000000b3", "", "rawframe.animation"));
    writeText(kRig / "whole.rfanim",
              *animation::writeMask(animation::Mask{.skeleton = kSkeletonId, .chains = {{.root = {1, 1}}}}));
    writeText(kRig / "whole.rfanim.rfmeta", sidecar("000000000000000000000000000000b4", "", "rawframe.animation"));
    static const std::array<Importer, 2> kImporters = {animationImporter(), audioImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    RAWFRAME_EXPECT(kCook().cooked == 6);
    // Each of its kind, its text as it was.
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    std::size_t kinds = 0;
    for (const content::ManifestEntry& each : kManifest.value_or(std::vector<content::ManifestEntry>{})) {
        if (each.id.value == kSkeletonId) {
            ++kinds;
            RAWFRAME_EXPECT(each.type.value == animation::kSkeletonType &&
                            each.representation.text() == animation::kSkeletonRepresentation &&
                            readText(kProject.output / each.locator) == kSkeletonText);
        }
        kinds += each.type.value == animation::kClipType || each.type.value == animation::kGraphType ||
                         each.type.value == animation::kMaskType
                     ? 1
                     : 0;
    }
    RAWFRAME_EXPECT(kinds == 4);
    // Not in its one form, or of no kind the importer knows: refused.
    writeText(kRig / "walk.rfanim", *animation::writeClip(kClip) + " ");
    writeText(kRig / "moves.rfanim", "{\"kind\": \"animation.pose\"}\n");
    RAWFRAME_EXPECT(kCook().failures.size() == 2);
}

RAWFRAME_TEST(ANondeterministicImporterIsCaught) {
    const Project kProject;
    static std::atomic<int> calls{0};
    static const std::array<Importer, 1> kWobbly = {
        Importer{.identity = "rawframe.audio",
                 .normalize = [](const document::Value*) -> result::Result<std::string> {
                     return std::string{"x"};
                 },
                 .cook = [](std::span<const std::byte>, std::string_view, Reads&) -> result::Result<Artifact> {
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
