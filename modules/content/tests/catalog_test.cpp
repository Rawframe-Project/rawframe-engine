// Manifests and the catalog against SPEC-0008's conformance list: identities
// that are not interchangeable, manifests read in any order and refused at
// the right field, locators that cannot escape or collide, a catalog that
// admits no duplicate and no unadmitted representation, and a fingerprint of
// meaning that ignores where bytes live.

#include "rawframe/content/catalog.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::content;

namespace {

constexpr ResourceTypeId kSoundType{base::Bits128{.high = 0x50, .low = 1}};
constexpr ResourceTypeId kTextureType{base::Bits128{.high = 0x50, .low = 2}};

ManifestEntry entry(std::uint64_t id, std::string locator, std::string_view bytes = "bytes") {
    return ManifestEntry{.id = ResourceId{base::Bits128{.high = 0, .low = id}},
                         .type = kSoundType,
                         .representation = *RepresentationId::parse("rawframe.audio.opus"),
                         .byteLength = bytes.size(),
                         .digest = ContentDigest::of(std::as_bytes(std::span{bytes.data(), bytes.size()})),
                         .locator = std::move(locator)};
}

std::vector<AdmittedRepresentation> admitted() {
    return {
        AdmittedRepresentation{.type = kSoundType, .representation = *RepresentationId::parse("rawframe.audio.opus")}};
}

bool refusedAs(const auto& attempt, ContentError error) {
    return !attempt.has_value() && attempt.error().domain() == kContentDomain && attempt.error().code() == code(error);
}

/// The manifest `writeManifest` makes of two entries, with `from` replaced
/// by `to`.
std::string manifestWith(std::string_view from, std::string_view to) {
    const std::vector<ManifestEntry> kEntries = {entry(2, "sounds/theme.rfopus"), entry(1, "sounds/shot.rfopus")};
    std::string text = writeManifest(kEntries);
    const std::size_t kAt = text.find(from);
    RAWFRAME_EXPECT(kAt != std::string::npos);
    if (kAt != std::string::npos) {
        text.replace(kAt, from.size(), to);
    }
    return text;
}

} // namespace

RAWFRAME_TEST(IdentitiesAreWhatTheySay) {
    RAWFRAME_EXPECT(RepresentationId::parse("rawframe.audio.opus").has_value());
    for (const std::string_view kBad : {"",
                                        "opus",
                                        "Rawframe.audio",
                                        "rawframe..opus",
                                        "rawframe.",
                                        ".opus",
                                        "rawframe.1opus",
                                        "rawframe.audio-opus"}) {
        RAWFRAME_EXPECT(!RepresentationId::parse(kBad).has_value());
    }
    const ContentDigest kDigest = ContentDigest::of(std::as_bytes(std::span{"abc", 3}));
    // FIPS 180-4's first example.
    RAWFRAME_EXPECT(kDigest.text() == "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    RAWFRAME_EXPECT(ContentDigest::parse(kDigest.text()) == kDigest);
    RAWFRAME_EXPECT(!ContentDigest::parse("sha512:ba78").has_value());
    RAWFRAME_EXPECT(
        !ContentDigest::parse("sha256:BA7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad").has_value());
    ContentDigest other = kDigest;
    other.bytes[31] ^= std::byte{1};
    RAWFRAME_EXPECT(sameDigest(kDigest, kDigest) && !sameDigest(kDigest, other));
    constexpr ResourceId kOne{base::Bits128{.high = 0, .low = 1}};
    RAWFRAME_EXPECT(!ResourceId{}.valid() && kOne.valid());
}

RAWFRAME_TEST(LocatorsNeitherEscapeNorCollide) {
    for (const std::string_view kGood : {"a", "sounds/shot.rfopus", "a/b-c/d_e.f", "..a", "a..b"}) {
        RAWFRAME_EXPECT(validLocator(kGood));
    }
    for (const std::string_view kBad : {"",
                                        "/sounds/shot",
                                        "sounds//shot",
                                        "../shot",
                                        "sounds/./shot",
                                        "sounds/..",
                                        "Sounds/shot",
                                        "c:shot",
                                        "sounds\\shot",
                                        "sounds/shot/",
                                        "http://x",
                                        "a b",
                                        "caf\xc3\xa9"}) {
        RAWFRAME_EXPECT(!validLocator(kBad));
    }
    RAWFRAME_EXPECT(!validLocator(std::string(300, 'a')));
    RAWFRAME_EXPECT(validLocator("a/b/c", {.maximumLocatorSegments = 3}) &&
                    !validLocator("a/b/c/d", {.maximumLocatorSegments = 3}));
}

RAWFRAME_TEST(AManifestReadsInAnyOrderAndWritesCanonically) {
    const std::vector<ManifestEntry> kEntries = {entry(2, "sounds/theme.rfopus"), entry(1, "sounds/shot.rfopus")};
    const std::string kText = writeManifest(kEntries);
    const auto kRead = readManifest(kText);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->size() == 2 && (*kRead)[0].id.value.low == 1 &&
                    (*kRead)[0].locator == "sounds/shot.rfopus" && (*kRead)[1].byteLength == 5 &&
                    (*kRead)[1].digest == kEntries[0].digest);
    RAWFRAME_EXPECT(kRead.has_value() && writeManifest(*kRead) == kText);
    // The writer sorts, and the reader takes either order.
    const std::vector<ManifestEntry> kSorted = {kEntries[1], kEntries[0]};
    RAWFRAME_EXPECT(writeManifest(kSorted) == kText);
    const std::size_t kFirst = kText.find("    {");
    const std::size_t kSecond = kText.find("    {", kFirst + 1);
    const std::size_t kEnd = kText.find("\n  ]");
    std::string swapped = kText.substr(0, kFirst) + kText.substr(kSecond, kEnd - kSecond) + ",\n" +
                          kText.substr(kFirst, kSecond - kFirst - 2) + kText.substr(kEnd);
    const auto kSwapped = readManifest(swapped);
    RAWFRAME_EXPECT(kSwapped.has_value() && kSwapped->size() == 2 && (*kSwapped)[0].id.value.low == 1);
}

RAWFRAME_TEST(EveryManifestRuleIsRefusedAsNamed) {
    struct Case {
        std::string_view from;
        std::string to;
        ContentError error;
    };
    const std::string kZero(32, '0');
    const std::vector<Case> kCases = {
        {"\"formatVersion\": 1", "\"formatVersion\": 2", ContentError::UnsupportedManifestVersion},
        {"\"content.manifest\"", "\"content.catalog\"", ContentError::ManifestInvalid},
        {"\"00000000000000000000000000000001\"", "\"" + kZero + "\"", ContentError::InvalidResourceId},
        {"\"00000000000000000000000000000001\"", "\"1\"", ContentError::InvalidResourceId},
        {"\"00000000000000000000000000000002\"",
         "\"00000000000000000000000000000001\"",
         ContentError::DuplicateResource},
        {"\"representation\": \"rawframe.audio.opus\"",
         "\"representation\": \"Opus\"",
         ContentError::UnsupportedRepresentation},
        {"\"byteLength\": 5", "\"byteLength\": -1", ContentError::ResourceTooLarge},
        {"\"byteLength\": 5", "\"byteLength\": 2147483648", ContentError::ResourceTooLarge},
        {"\"digest\": \"sha256:", "\"digest\": \"md5:", ContentError::ManifestInvalid},
        {"\"sounds/shot.rfopus\"", "\"../shot.rfopus\"", ContentError::InvalidLocator},
        {"\"sounds/shot.rfopus\"", "\"/etc/passwd\"", ContentError::InvalidLocator},
    };
    for (const Case& each : kCases) {
        const std::string kText = manifestWith(each.from, each.to);
        const auto kRead = readManifest(kText);
        RAWFRAME_EXPECT(refusedAs(kRead, each.error));
        if (!refusedAs(kRead, each.error)) {
            std::fprintf(stderr, "  replacing %.*s\n", static_cast<int>(each.from.size()), each.from.data());
        }
    }
    // The type's zero is its own refusal.
    const std::string kTypeHex = "\"00000000000000500000000000000001\"";
    RAWFRAME_EXPECT(
        refusedAs(readManifest(manifestWith(kTypeHex, "\"" + kZero + "\"")), ContentError::InvalidResourceType));
    RAWFRAME_EXPECT(
        refusedAs(readManifest(manifestWith("", ""), {.maximumEntries = 1}), ContentError::ManifestInvalid));
    // An unknown field is the document profile's refusal.
    const auto kUnknown = readManifest(manifestWith("\"locator\"", "\"path\""));
    RAWFRAME_EXPECT(!kUnknown.has_value() && kUnknown.error().domain() != kContentDomain);
}

RAWFRAME_TEST(TheCatalogCombinesManifestsOrNothing) {
    const std::vector<BoundManifest> kManifests = {
        BoundManifest{.entries = {entry(1, "shot.rfopus"), entry(3, "hit.rfopus")}, .source = 0},
        BoundManifest{.entries = {entry(2, "theme.rfopus")}, .source = 1},
    };
    const auto kCatalog = ContentCatalog::build(kManifests, admitted(), 2, 7);
    RAWFRAME_EXPECT(kCatalog.has_value() && (*kCatalog)->size() == 3 && (*kCatalog)->generation() == 7);
    if (!kCatalog.has_value()) {
        return;
    }
    const ContentCatalog& catalog = **kCatalog;
    const ResourceId kTheme{base::Bits128{.high = 0, .low = 2}};
    const auto kFound = catalog.lookup(kTheme);
    RAWFRAME_EXPECT(kFound.has_value() && (*kFound)->source == 1 && (*kFound)->locator == "theme.rfopus");
    RAWFRAME_EXPECT(
        refusedAs(catalog.lookup(ResourceId{base::Bits128{.high = 0, .low = 9}}), ContentError::ResourceNotFound));
    RAWFRAME_EXPECT(catalog.resolve(ResourceRef{.id = kTheme, .type = kSoundType}).has_value());
    RAWFRAME_EXPECT(refusedAs(catalog.resolve(ResourceRef{.id = kTheme, .type = kTextureType}),
                              ContentError::ResourceTypeMismatch));
    const ContentDigest kRight = entry(2, "x").digest;
    ContentDigest wrong = kRight;
    wrong.bytes[0] ^= std::byte{1};
    RAWFRAME_EXPECT(catalog.resolve(PinnedResourceRef{.resource = {.id = kTheme, .type = kSoundType}, .digest = kRight})
                        .has_value());
    RAWFRAME_EXPECT(
        refusedAs(catalog.resolve(PinnedResourceRef{.resource = {.id = kTheme, .type = kSoundType}, .digest = wrong}),
                  ContentError::RevisionMismatch));

    // Nothing wins over anything: a resource in two manifests, even
    // identically, refuses the whole catalog.
    std::vector<BoundManifest> twice = kManifests;
    twice[1].entries.push_back(entry(1, "shot.rfopus"));
    RAWFRAME_EXPECT(refusedAs(ContentCatalog::build(twice, admitted(), 2, 7), ContentError::DuplicateResource));
    RAWFRAME_EXPECT(refusedAs(ContentCatalog::build(kManifests, admitted(), 1, 7), ContentError::SourceUnavailable));
    RAWFRAME_EXPECT(refusedAs(ContentCatalog::build(kManifests, {}, 2, 7), ContentError::UnsupportedRepresentation));
}

RAWFRAME_TEST(TheFingerprintIsOfMeaningNotPlace) {
    const auto kFingerprint = [](const std::vector<BoundManifest>& manifests) {
        return (*ContentCatalog::build(manifests, admitted(), 2, 1))->fingerprint();
    };
    const std::vector<BoundManifest> kBase = {
        BoundManifest{.entries = {entry(1, "shot.rfopus"), entry(3, "hit.rfopus")}, .source = 0},
        BoundManifest{.entries = {entry(2, "theme.rfopus")}, .source = 1},
    };
    // Manifest and entry order, locators, and sources change nothing.
    std::vector<BoundManifest> reordered = {kBase[1], kBase[0]};
    std::ranges::reverse(reordered[1].entries);
    RAWFRAME_EXPECT(kFingerprint(reordered) == kFingerprint(kBase));
    std::vector<BoundManifest> moved = kBase;
    moved[0].entries[0].locator = "weapons/shot.rfopus";
    moved[1].source = 0;
    RAWFRAME_EXPECT(kFingerprint(moved) == kFingerprint(kBase));
    // New bytes, a new length, or another representation change it.
    std::vector<BoundManifest> edited = kBase;
    edited[0].entries[0] = entry(1, "shot.rfopus", "other");
    RAWFRAME_EXPECT(kFingerprint(edited) != kFingerprint(kBase));
    std::vector<BoundManifest> longer = kBase;
    longer[0].entries[0].byteLength = 6;
    RAWFRAME_EXPECT(kFingerprint(longer) != kFingerprint(kBase));
}

RAWFRAME_TEST(HostileManifestsAndCompositionsReadOnlyAsTheyWrite) {
    // A Build's manifest is signed by its publisher, a mod's by someone the
    // game does not trust, and a Composition names Builds a client fetches
    // (D190): seeded mutations of each. A manifest read in any order writes
    // its canonical form, which reads back to the same entries; a
    // Composition is canonical and writes back to its very bytes.
    const std::vector<ManifestEntry> kEntries = {entry(2, "sounds/theme.rfopus"), entry(1, "sounds/shot.rfopus")};
    const std::string kManifest = writeManifest(kEntries);
    base::Sha256Digest root{};
    root.fill(std::byte{0xab});
    const auto kComposition = writeComposition(
        CompositionRecord{.game = {.subject = "rawframe/runners", .version = "0.1.0", .build = root},
                          .mods = {{.subject = "fan/horde", .version = "1.0.0", .build = root}},
                          .packages = {{.subject = "rawframe/sounds", .version = "1.0.0", .build = root}},
                          .profile = "community",
                          .createdAt = 1'790'000'000});
    RAWFRAME_EXPECT(kComposition.has_value());
    if (!kComposition.has_value()) {
        return;
    }
    constexpr std::string_view kInserted = "{}[]\",: \n./-_0123456789abcdef";
    test::Mutations mutations;
    int manifests = 0;
    int compositions = 0;
    for (int round = 0; round < 20'000; ++round) {
        const std::string kText = mutations.mutate(kManifest, kInserted);
        if (const auto kRead = readManifest(kText)) {
            ++manifests;
            const std::string kWritten = writeManifest(*kRead);
            const auto kAgain = readManifest(kWritten);
            RAWFRAME_EXPECT(kAgain.has_value() && writeManifest(*kAgain) == kWritten &&
                            kAgain->size() == kRead->size());
        }
        const std::string kRecord = mutations.mutate(*kComposition, kInserted);
        if (const auto kRead = readComposition(kRecord)) {
            ++compositions;
            RAWFRAME_EXPECT(writeComposition(*kRead).value_or("") == kRecord);
        }
    }
    std::printf("  accepted: %d manifests, %d compositions of 20000 each\n", manifests, compositions);
    RAWFRAME_EXPECT(manifests > 0 && compositions > 0);
}
