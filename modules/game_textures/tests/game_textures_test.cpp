// A game's textures read decoded by identity from cooked content: a texture
// that decodes is ready and held, one that does not fails and is reported
// once, one the content does not hold is refused, and a recooked texture is
// seen at its new revision, or its old one kept when the new one is broken;
// and textures asked for one by one are read on their first asking, one the
// game does not declare said once (D378).

#include "rawframe/game_textures/asked.h"
#include "rawframe/game_textures/game_textures.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#if RAWFRAME_THREADS
#include <thread>
#endif

using namespace rawframe;
using namespace rawframe::game_textures;

namespace {

constexpr std::uint64_t kRunner = 0xb1;
constexpr std::uint64_t kTiles = 0xb2;

/// A content store over two cooked textures, as a client composes it from a
/// game's cooked output: 1 is a 2 by 2 texture, 2's bytes are no texture.
/// `recook` publishes 1 again as a 4 by 4 texture, or as broken bytes.
struct Content {
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    execution::Executor cpu{execution::ExecutorSettings{.kind = execution::ExecutorKind::Cpu, .workers = 1}};
    std::unique_ptr<content::ContentStore> store;
    const std::vector<std::byte> kGood = cooked();
    const std::vector<std::byte> kBroken = std::vector<std::byte>(64, std::byte{7});
    const std::vector<std::byte> kLarger = cooked(4);
    const std::vector<std::byte> kBrokenAgain = std::vector<std::byte>(80, std::byte{7});
    std::uint64_t generation = 0;

    static std::vector<std::byte> cooked(std::uint32_t side = 2) {
        texture::Texture made{.format = texture::Format::Rgba8Srgb};
        made.levels.push_back(
            texture::Level{.width = side,
                           .height = side,
                           .bytes = std::vector<std::byte>(std::size_t{side} * side * 4, std::byte{9})});
        return *texture::encode(made);
    }

    static content::ManifestEntry
    entryOf(std::uint64_t id, const std::string& locator, const std::vector<std::byte>& bytes) {
        return content::ManifestEntry{.id = content::ResourceId{base::Bits128{.high = 0, .low = id}},
                                      .type = content::ResourceTypeId{texture::kTextureType},
                                      .representation =
                                          *content::RepresentationId::parse(texture::kTextureRepresentation),
                                      .byteLength = bytes.size(),
                                      .digest = content::ContentDigest::of(bytes),
                                      .locator = locator};
    }

    Content() {
        RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 16}).has_value());
        RAWFRAME_EXPECT(cpu.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 16}).has_value());
        std::vector<content::ContentSource> sources;
        sources.push_back(std::move(
            *content::ContentSource::memory({{"a", kGood}, {"b", kBroken}, {"c", kLarger}, {"d", kBrokenAgain}})));
        store = std::move(*content::ContentStore::create(io, execution::OwnerId{1}, root, clock, std::move(sources)));
        publish(entryOf(1, "a", kGood));
    }

    void publish(const content::ManifestEntry& first) {
        ++generation;
        const std::vector<content::BoundManifest> kManifests = {
            {.entries = {first, entryOf(2, "b", kBroken)}, .source = 0}};
        store->publish(*content::ContentCatalog::build(kManifests, textureRepresentations(), generation, generation));
    }

    void recook(bool broken) {
        publish(broken ? entryOf(1, "d", kBrokenAgain) : entryOf(1, "c", kLarger));
    }
    ~Content() {
        store.reset();
        cpu.stop();
        io.stop();
    }
    Content(const Content&) = delete;
    Content& operator=(const Content&) = delete;

    result::Result<std::unique_ptr<GameTextures>> textures(std::vector<world_kest::GameTextureResource> declared) {
        return GameTextures::create(*store, cpu, execution::OwnerId{1}, root, clock, std::move(declared), 1U << 20U);
    }

    /// Updates `textures` until none is pending and `until` holds of what
    /// changed, within ten seconds; what changed together.
    template <typename Until = bool (*)(const TextureChanges&)>
    TextureChanges settle(
        GameTextures& textures, std::uint64_t tick = 1, Until until = [](const TextureChanges&) {
            return true;
        }) {
        TextureChanges changed;
        const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < kDeadline) {
            TextureChanges each = textures.update(tick);
            std::ranges::move(each.failed, std::back_inserter(changed.failed));
            std::ranges::move(each.reloaded, std::back_inserter(changed.reloaded));
            std::ranges::move(each.notReloaded, std::back_inserter(changed.notReloaded));
            changed.read = changed.read || each.read;
            if (textures.counts().pending == 0 && until(changed)) {
                break;
            }
#if RAWFRAME_THREADS
            std::this_thread::yield();
#else
            while (io.runOne() || cpu.runOne()) {
            }
#endif
        }
        return changed;
    }
};

world_kest::GameTextureResource declared(std::uint64_t id, std::uint64_t resource) {
    return {.id = id, .path = "t" + std::to_string(id) + ".png", .texture = base::Bits128{.high = 0, .low = resource}};
}

} // namespace

RAWFRAME_TEST(TexturesAreReadByIdentityFromCookedContent) {
    Content content;
    auto textures = content.textures({declared(kRunner, 1), declared(kTiles, 2)});
    RAWFRAME_EXPECT(textures.has_value());
    if (!textures.has_value()) {
        return;
    }
    // The good one decodes to its levels; the broken one fails, reported
    // once, by the identity the game gives it.
    const TextureChanges kSettled = content.settle(**textures);
    RAWFRAME_EXPECT(kSettled.failed.size() == 1 && kSettled.failed[0].first == kTiles && !kSettled.read &&
                    (*textures)->update(1).failed.empty());
    const auto kRunnerTexture = (*textures)->texture(kRunner, 1);
    RAWFRAME_EXPECT(kRunnerTexture != nullptr && kRunnerTexture->levels.size() == 1 &&
                    kRunnerTexture->levels[0].width == 2 && kRunnerTexture->format == texture::Format::Rgba8Srgb);
    RAWFRAME_EXPECT((*textures)->texture(kTiles, 1) == nullptr && (*textures)->texture(0xdead, 1) == nullptr);
    const TextureCounts kCounts = (*textures)->counts();
    RAWFRAME_EXPECT(kCounts.ready == 1 && kCounts.failed == 1 && kCounts.pending == 0 && kCounts.bytes == 16);
    // A texture the content does not hold is refused when asked for.
    RAWFRAME_EXPECT(!content.textures({declared(kRunner, 9)}).has_value());
}

RAWFRAME_TEST(ARecookedTextureIsDrawnAtItsNewRevision) {
    Content content;
    auto textures = content.textures({declared(kRunner, 1)});
    RAWFRAME_EXPECT(textures.has_value());
    if (!textures.has_value()) {
        return;
    }
    // Read once, said once.
    RAWFRAME_EXPECT(content.settle(**textures).read && !(*textures)->update(1).read);
    // Recooked larger: the new revision replaces the old, and is what is
    // drawn.
    content.recook(false);
    const auto kReloaded = [](const TextureChanges& changes) {
        return !changes.reloaded.empty() || !changes.notReloaded.empty();
    };
    const TextureChanges kLarger = content.settle(**textures, 2, kReloaded);
    RAWFRAME_EXPECT(kLarger.reloaded == std::vector<std::uint64_t>{kRunner} && kLarger.notReloaded.empty() &&
                    (*textures)->texture(kRunner, 2) != nullptr &&
                    (*textures)->texture(kRunner, 2)->levels[0].width == 4);
    // Recooked broken: the new revision cannot be made, and the old one is
    // drawn on.
    content.recook(true);
    const TextureChanges kBroken = content.settle(**textures, 3, kReloaded);
    RAWFRAME_EXPECT(kBroken.reloaded.empty() && kBroken.notReloaded.size() == 1 &&
                    kBroken.notReloaded[0].first == kRunner && (*textures)->texture(kRunner, 3) != nullptr &&
                    (*textures)->texture(kRunner, 3)->levels[0].width == 4 && (*textures)->counts().ready == 1);
}

RAWFRAME_TEST(ATextureAskedForIsReadOnItsFirstAsking) {
    Content content;
    AskedTextures asked{TextureReading{.store = content.store.get(),
                                       .cpu = &content.cpu,
                                       .owner = execution::OwnerId{1},
                                       .scope = &content.root,
                                       .clock = &content.clock},
                        {declared(kRunner, 1), declared(kTiles, 2)},
                        1U << 20U};
    // Nought is none; an undeclared one is said once; a declared one is read.
    RAWFRAME_EXPECT(!asked.ask(0).has_value() && asked.read() == 0);
    const std::optional<result::Error> kUnknown = asked.ask(0xdead);
    RAWFRAME_EXPECT(kUnknown.has_value() && kUnknown->errorClass() == result::ErrorClass::NotFound &&
                    !asked.ask(0xdead).has_value());
    RAWFRAME_EXPECT(!asked.ask(kRunner).has_value() && !asked.ask(kRunner).has_value() && asked.read() == 1);
    RAWFRAME_EXPECT(asked.texture(kTiles, 1) == nullptr);
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (asked.texture(kRunner, 1) == nullptr && std::chrono::steady_clock::now() < kDeadline) {
        RAWFRAME_EXPECT(asked.update(1).empty());
#if RAWFRAME_THREADS
        std::this_thread::yield();
#else
        while (content.io.runOne() || content.cpu.runOne()) {
        }
#endif
    }
    RAWFRAME_EXPECT(asked.texture(kRunner, 1) != nullptr && asked.ready() == 1);
    // The broken one, asked for, fails once.
    RAWFRAME_EXPECT(!asked.ask(kTiles).has_value());
    std::vector<std::pair<std::uint64_t, result::Error>> failed;
    while (failed.empty() && std::chrono::steady_clock::now() < kDeadline + std::chrono::seconds(10)) {
        failed = asked.update(1);
#if RAWFRAME_THREADS
        std::this_thread::yield();
#else
        while (content.io.runOne() || content.cpu.runOne()) {
        }
#endif
    }
    RAWFRAME_EXPECT(failed.size() == 1 && failed[0].first == kTiles && asked.update(1).empty());
}
