// Game descriptions as hostile input: every sample game's description,
// mutated a line or a byte at a time, is read or refused with the line that
// refused it, never half read.

#include "game_harness.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;

RAWFRAME_TEST(HostileGameDescriptionsAreReadOrRefusedAtALine) {
    // A game's description comes with content a client fetched, from a
    // publisher the player may never have met.
    constexpr std::array<std::string_view, 5> kGames = {
        "arena/arena.game", "crates/crates.game", "plaza/plaza.game", "runners/duel.game", "runners/runners.game"};
    constexpr std::array<std::string_view, 10> kLines = {"program other.kest\n",
                                                         "scene level.scene\n",
                                                         "mods closed\n",
                                                         "modapi runners 2\n",
                                                         "extension more data runners.age exclusive\n",
                                                         "interest rawframe.physics2d.pose x y within 1e40\n",
                                                         "predict runners.score\n",
                                                         "input runners.score\n",
                                                         "physics3d gravity 0 -10 0\n",
                                                         "save hall runners.stick\n"};
    std::size_t read = 0;
    std::size_t refused = 0;
    test::Mutations mutations;
    for (const std::string_view kGame : kGames) {
        const std::string kSeed = game_test::readText(std::string{RAWFRAME_SAMPLE_GAMES} + std::string{kGame});
        RAWFRAME_EXPECT(world_kest::parseGame(kSeed).has_value());
        for (int round = 0; round < 4'000; ++round) {
            const std::string kText = mutations.mutate(kSeed, " \t\r\n#.-_09aZ\xC3", kLines);
            const auto kParsed = world_kest::parseGame(kText);
            if (kParsed.has_value()) {
                ++read;
                RAWFRAME_EXPECT(!kParsed->program.empty());
                continue;
            }
            ++refused;
            const auto kContext = kParsed.error().context();
            RAWFRAME_EXPECT(kParsed.error().domain() == world_kest::kWorldKestDomain &&
                            std::ranges::any_of(kContext, [](const result::ContextField& field) {
                                return field.key == "line";
                            }));
        }
    }
    std::printf("  %zu of %zu read\n", read, read + refused);
    RAWFRAME_EXPECT(read > 0 && refused > 0);
}
