// Bot input made as a player's is: a hand on the controls the sample game
// binds, its action set, and its Kest sample function, deterministic by
// seed; games without controls, or whose sample does not fit, refused.

#include "rawframe/input_kest/errors.h"
#include "rawframe/input_kest/sources.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::input_kest;

namespace {

/// runners.Stick: run, jump, aimX, aimY, fire.
using Stick = std::array<float, 5>;

SourceSettings runners(std::size_t inputSize = sizeof(Stick)) {
    SourceSettings settings{.game = std::string{RAWFRAME_SAMPLE_GAMES} + "runners/runners.game",
                            .inputSize = inputSize};
    settings.compile.library = RAWFRAME_KEST_LIBRARY;
    return settings;
}

std::vector<Stick> play(world_replication::InputSource& source, int ticks) {
    std::vector<Stick> played;
    std::array<std::byte, sizeof(Stick)> input{};
    for (int tick = 1; tick <= ticks; ++tick) {
        RAWFRAME_EXPECT(source.next(static_cast<std::uint64_t>(tick), input).has_value());
        Stick stick{};
        std::memcpy(stick.data(), input.data(), sizeof stick);
        played.push_back(stick);
    }
    return played;
}

} // namespace

RAWFRAME_TEST(ABotPlaysThroughTheGamesActionsAndSample) {
    auto sources = makeInputSources(runners());
    RAWFRAME_EXPECT(sources.has_value());
    if (!sources.has_value()) {
        return;
    }
    auto source = (*sources)->botSource(7);
    RAWFRAME_EXPECT(source.has_value());
    if (!source.has_value()) {
        return;
    }
    const std::vector<Stick> kPlayed = play(**source, 1200);
    std::set<float> runs;
    std::set<float> jumps;
    std::set<float> fires;
    int aimed = 0;
    for (const Stick& stick : kPlayed) {
        runs.insert(stick[0]);
        jumps.insert(stick[1]);
        fires.insert(stick[4]);
        const float kAim = std::hypot(stick[2], stick[3]);
        RAWFRAME_EXPECT(kAim <= 1.0001F);
        aimed += kAim > 0 ? 1 : 0;
    }
    // Keys and pads make whole steps: run is left, still, or right; jump and
    // fire are on or off; and every one of them happened.
    RAWFRAME_EXPECT((runs == std::set<float>{-1, 0, 1}));
    RAWFRAME_EXPECT((jumps == std::set<float>{0, 1}) && (fires == std::set<float>{0, 1}));
    RAWFRAME_EXPECT(aimed > 100);

    // The same seed plays the same; another does not.
    auto again = (*sources)->botSource(7);
    auto other = (*sources)->botSource(8);
    RAWFRAME_EXPECT(again.has_value() && other.has_value());
    if (again.has_value() && other.has_value()) {
        RAWFRAME_EXPECT(play(**again, 1200) == kPlayed);
        RAWFRAME_EXPECT(play(**other, 1200) != kPlayed);
    }
}

RAWFRAME_TEST(GamesWithoutControlsOrWithAMisfitSampleAreRefused) {
    SourceSettings crates = runners();
    crates.game = std::string{RAWFRAME_SAMPLE_GAMES} + "crates/crates.game";
    const auto kCrates = makeInputSources(crates);
    RAWFRAME_EXPECT(!kCrates.has_value() && kCrates.error().code() == code(InputKestError::NoControls) &&
                    kCrates.error().errorClass() == result::ErrorClass::NotFound);
    const auto kMisfit = makeInputSources(runners(sizeof(Stick) + 4));
    RAWFRAME_EXPECT(!kMisfit.has_value() && kMisfit.error().code() == code(InputKestError::BadSample));
}
