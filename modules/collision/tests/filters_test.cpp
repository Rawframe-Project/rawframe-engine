#include "rawframe/collision/errors.h"
#include "rawframe/collision/filters.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::collision;

namespace {

constexpr std::uint64_t kPlayer = 0x11;
constexpr std::uint64_t kGhost = 0x22;
constexpr std::uint64_t kCoin = 0x33;

} // namespace

RAWFRAME_TEST(RulesBecomeMaulBits) {
    const auto kFilters = CollisionFilters::make(
        {.classes = {{kPlayer, "player"}, {kGhost, "ghost"}, {kCoin, "coin"}},
         .rules = {{kPlayer, kGhost, CollisionRule::Ignore}, {kCoin, kPlayer, CollisionRule::Trigger}}});
    RAWFRAME_EXPECT(kFilters.has_value());
    if (!kFilters.has_value()) {
        return;
    }
    const std::size_t kP = *kFilters->classIndex(kPlayer);
    const std::size_t kG = *kFilters->classIndex(kGhost);
    const std::size_t kC = *kFilters->classIndex(kCoin);
    RAWFRAME_EXPECT(kFilters->classIndex(0) == 0 && !kFilters->classIndex(0x44).has_value());
    const ClassFilter& kPlayers = kFilters->filter(kP);
    // Players meet bodies of no class and coins' sensors, pass through
    // ghosts, and trigger with coins rather than push them.
    RAWFRAME_EXPECT((kPlayers.solidMask & CollisionFilters::solidBit(0)) != 0);
    RAWFRAME_EXPECT((kPlayers.solidMask & CollisionFilters::solidBit(kG)) == 0);
    RAWFRAME_EXPECT((kPlayers.solidMask & CollisionFilters::sensorBit(kG)) == 0);
    RAWFRAME_EXPECT((kPlayers.solidMask & CollisionFilters::solidBit(kC)) == 0);
    RAWFRAME_EXPECT((kPlayers.solidMask & CollisionFilters::sensorBit(kC)) != 0);
    RAWFRAME_EXPECT(kPlayers.triggerMask == CollisionFilters::solidBit(kC));
    // Every solid meets the character controller's queries.
    for (const std::size_t kIndex : {std::size_t{0}, kP, kG, kC}) {
        RAWFRAME_EXPECT((kFilters->filter(kIndex).solidMask & kCharacterQuery) != 0);
    }
    RAWFRAME_EXPECT((kSolids & kCharacterQuery) == 0 && (kSolids & CollisionFilters::solidBit(30)) != 0);
}

RAWFRAME_TEST(ADocumentThatIsNotWellFormedIsRefused) {
    const std::vector<CollisionDocument> kBad = {
        {.classes = {{0, "none"}}},
        {.classes = {{kPlayer, ""}}},
        {.classes = {{kPlayer, "player"}, {kPlayer, "again"}}},
        {.classes = {{kPlayer, "player"}, {kGhost, "player"}}},
        {.classes = {{kPlayer, "player"}}, .rules = {{kPlayer, kGhost, CollisionRule::Ignore}}},
        {.classes = {{kPlayer, "player"}, {kGhost, "ghost"}},
         .rules = {{kPlayer, kGhost, CollisionRule::Ignore}, {kGhost, kPlayer, CollisionRule::Trigger}}},
    };
    for (const CollisionDocument& kDocument : kBad) {
        const auto kMade = CollisionFilters::make(kDocument);
        RAWFRAME_EXPECT(!kMade.has_value() && kMade.error().code() == code(CollisionError::InvalidDocument));
    }
    CollisionDocument crowded;
    for (std::uint64_t index = 1; index <= kMaximumCollisionClasses; ++index) {
        crowded.classes.push_back({index, "class" + std::to_string(index)});
    }
    RAWFRAME_EXPECT(CollisionFilters::make(crowded).has_value());
    crowded.classes.push_back({kMaximumCollisionClasses + 1, "one more"});
    RAWFRAME_EXPECT(!CollisionFilters::make(crowded).has_value());
}
