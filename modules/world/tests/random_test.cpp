// Deterministic randomness: published reference vectors, pinned derivation
// vectors, the value constructions, and streams inside the schedule.

#include "components.h"
#include "rawframe/test/test.h"
#include "rawframe/world/errors.h"
#include "rawframe/world/random.h"
#include "rawframe/world/schedule.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace rawframe::world;
using namespace rawframe::world::testing;
using rawframe::result::Status;

RAWFRAME_TEST(Pcg32MatchesThePublishedReference) {
    // pcg32-demo: pcg32_srandom_r(&rng, 42u, 54u).
    Pcg32 generator{42, 54};
    constexpr std::array<std::uint32_t, 6> kExpected = {
        0xa15c02b7U, 0x7b47f409U, 0xba1d3330U, 0x83d2f293U, 0xbfa4784bU, 0xcbed606eU};
    for (const std::uint32_t kValue : kExpected) {
        RAWFRAME_EXPECT(generator.nextU32() == kValue);
    }
    static_assert(Pcg32{42, 54}.nextU32() == 0xa15c02b7U, "usable at compile time");
}

RAWFRAME_TEST(SplitMix64MatchesThePublishedReference) {
    std::uint64_t state = 0;
    RAWFRAME_EXPECT(splitMix64(state) == 0xe220a8397b1dcdafULL);
    RAWFRAME_EXPECT(splitMix64(state) == 0x6e789e6aa1b965f4ULL);
    RAWFRAME_EXPECT(splitMix64(state) == 0x06c45d188009454fULL);
}

RAWFRAME_TEST(DerivationIsPinned) {
    // Computed by an independent mirror of the specification; a change here
    // is a new random identity, not a fix.
    Pcg32 stream = deriveStream(RootSeed{1234}, "movement", "jitter");
    constexpr std::array<std::uint32_t, 4> kStream = {0x6456128cU, 0x704bc5ccU, 0x62fe7b79U, 0x5ee30609U};
    for (const std::uint32_t kValue : kStream) {
        RAWFRAME_EXPECT(stream.nextU32() == kValue);
    }
    RAWFRAME_EXPECT(indexedDraw(RootSeed{1234}, "particles", "sparks", 0) == 0x8ecc1524b027fbe0ULL);
    RAWFRAME_EXPECT(indexedDraw(RootSeed{1234}, "particles", "sparks", 1) == 0x79a8edd19d455636ULL);
    RAWFRAME_EXPECT(indexedDraw(RootSeed{1234}, "particles", "sparks", 1000) == 0xa14b3b4b0d40980aULL);

    Pcg32 loot = deriveStream(RootSeed{7}, "loot", "roll");
    constexpr std::array<std::uint32_t, 8> kBelowTen = {6, 9, 6, 6, 4, 9, 6, 6};
    for (const std::uint32_t kValue : kBelowTen) {
        RAWFRAME_EXPECT(loot.nextBelow(10) == kValue);
    }
}

RAWFRAME_TEST(StreamsDependOnlyOnTheirIdentity) {
    const RootSeed kRoot{99};
    Pcg32 first = deriveStream(kRoot, "system_a", "one");
    Pcg32 second = deriveStream(kRoot, "system_b", "one");
    // Drawing from one stream never changes another, whatever the order.
    for (int draw = 0; draw < 100; ++draw) {
        static_cast<void>(second.nextU32());
    }
    RAWFRAME_EXPECT(first == deriveStream(kRoot, "system_a", "one"));
    RAWFRAME_EXPECT(deriveStream(kRoot, "system_a", "one") != deriveStream(kRoot, "system_a", "two"));
    RAWFRAME_EXPECT(deriveStream(kRoot, "system_a", "one") != deriveStream(RootSeed{100}, "system_a", "one"));
    // A captured state restores exactly.
    Pcg32 restored = Pcg32::fromWords(second.stateWord(), second.incrementWord());
    RAWFRAME_EXPECT(restored.nextU64() == second.nextU64());
}

RAWFRAME_TEST(ValueConstructionsStayInRange) {
    Pcg32 generator = deriveStream(RootSeed{5}, "test", "values");
    std::array<std::uint32_t, 7> buckets{};
    for (int draw = 0; draw < 70000; ++draw) {
        const std::uint32_t kValue = generator.nextBelow(7);
        RAWFRAME_EXPECT(kValue < 7);
        ++buckets[kValue];
        const double kDouble = generator.nextDouble();
        RAWFRAME_EXPECT(kDouble >= 0.0 && kDouble < 1.0);
        const float kFloat = generator.nextFloat();
        RAWFRAME_EXPECT(kFloat >= 0.0F && kFloat < 1.0F);
        RAWFRAME_EXPECT(generator.nextBelow64(3'000'000'000'000ULL) < 3'000'000'000'000ULL);
    }
    // Loosely uniform: each of 7 buckets near 10000.
    for (const std::uint32_t kCount : buckets) {
        RAWFRAME_EXPECT(kCount > 9500 && kCount < 10500);
    }
    RAWFRAME_EXPECT(generator.nextBelow(1) == 0);
}

RAWFRAME_TEST(ShuffleAndWeightedChoiceAreDeterministic) {
    std::array<int, 10> first{};
    std::iota(first.begin(), first.end(), 0);
    std::array<int, 10> second = first;
    Pcg32 left = deriveStream(RootSeed{3}, "deck", "shuffle");
    Pcg32 right = deriveStream(RootSeed{3}, "deck", "shuffle");
    shuffle(std::span<int>{first}, left);
    shuffle(std::span<int>{second}, right);
    RAWFRAME_EXPECT(first == second);
    std::array<int, 10> sorted = first;
    std::sort(sorted.begin(), sorted.end());
    RAWFRAME_EXPECT(sorted == (std::array<int, 10>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
    RAWFRAME_EXPECT(first != sorted);

    constexpr std::array<std::uint64_t, 4> kWeights = {0, 5, 0, 15};
    std::array<int, 4> chosen{};
    for (int draw = 0; draw < 4000; ++draw) {
        ++chosen[weightedChoice(kWeights, left)];
    }
    RAWFRAME_EXPECT(chosen[0] == 0 && chosen[2] == 0);
    RAWFRAME_EXPECT(chosen[1] > 850 && chosen[1] < 1150);
    constexpr std::array<std::uint64_t, 2> kNothing = {0, 0};
    RAWFRAME_EXPECT(weightedChoice(kNothing, left) == 2);
}

namespace {

/// Draws once per tick from its declared stream and remembers the values.
class Roller final : public System {
public:
    Status run(SystemContext& context) noexcept override {
        RAWFRAME_TRY_ASSIGN(Pcg32* const kStream, context.random("roll"));
        rolls.push_back(kStream->nextBelow(1000));
        undeclaredRefused = !context.random("other").has_value();
        return {};
    }
    std::vector<std::uint32_t> rolls;
    bool undeclaredRefused = false;
};

std::vector<std::uint32_t> rollsFor(RootSeed seed) {
    const auto kRegistry = makeRegistry();
    World world{kRegistry, WorldSettings{.rootSeed = seed}};
    Roller roller;
    constexpr std::string_view kStreams[] = {"roll"};
    const SystemDeclaration kDeclarations[] = {{.identity = "roller", .randomStreams = kStreams, .system = &roller}};
    auto schedule = Schedule::compile(kDeclarations, *kRegistry);
    TickIndex tick;
    for (int round = 0; round < 20; ++round) {
        static_cast<void>(schedule->runTick(world, tick, TickRate{}));
    }
    RAWFRAME_EXPECT(roller.undeclaredRefused);
    return roller.rolls;
}

} // namespace

RAWFRAME_TEST(SystemStreamsFollowTheWorldSeed) {
    const auto kFirst = rollsFor(RootSeed{11});
    RAWFRAME_EXPECT(kFirst.size() == 20);
    RAWFRAME_EXPECT(rollsFor(RootSeed{11}) == kFirst);
    RAWFRAME_EXPECT(rollsFor(RootSeed{12}) != kFirst);

    std::vector<std::string> log;
    Roller roller;
    const auto kRegistry = makeRegistry();
    constexpr std::string_view kBadName[] = {"Roll"};
    constexpr std::string_view kTwice[] = {"roll", "roll"};
    const SystemDeclaration kBad[] = {{.identity = "roller", .randomStreams = kBadName, .system = &roller}};
    const SystemDeclaration kDuplicate[] = {{.identity = "roller", .randomStreams = kTwice, .system = &roller}};
    RAWFRAME_EXPECT(!Schedule::compile(kBad, *kRegistry).has_value());
    RAWFRAME_EXPECT(!Schedule::compile(kDuplicate, *kRegistry).has_value());
}
