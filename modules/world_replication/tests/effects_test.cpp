// SPEC-0041's effect ledger (D219): a predicted effect reaches presentation
// once however often resimulation emits it, one a resimulation no longer
// emits is cancelled once, and a confirmed-only effect waits for the
// server's state to confirm its tick.

#include "../src/prediction.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstring>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_replication;

namespace {

constexpr schema::ComponentTypeId kCounter = schema::ComponentTypeId::fromText("5a1d3c7e-2b4f-4e6a-9c8d-7f0e1b2a3c4d");

/// A counter each step adds its input to, emitting kind 0 (predicted) when
/// it reaches a multiple of five and kind 1 (confirmed only) of three.
class Counting final : public Predictor {
public:
    std::span<const std::byte> get(schema::ComponentTypeId) const noexcept override {
        return std::as_bytes(std::span{&value_, 1});
    }
    result::Status set(schema::ComponentTypeId, std::span<const std::byte> value) override {
        std::memcpy(&value_, value.data(), sizeof value_);
        return {};
    }
    result::Status step(std::span<const std::byte> input) override {
        std::int32_t add = 0;
        std::memcpy(&add, input.data(), sizeof add);
        value_ += add;
        effects_.clear();
        if (value_ % 5 == 0) {
            effects_.push_back(StepEffect{.kind = 0, .system = 7, .ordinal = 0});
        }
        if (value_ % 3 == 0) {
            effects_.push_back(StepEffect{.kind = 1, .system = 7, .ordinal = 0});
        }
        return {};
    }
    void rate(world::TickRate) noexcept override {
    }
    result::Status place(std::span<const NeighborValue>) override {
        return {};
    }
    std::span<const StepEffect> effects() const noexcept override {
        return effects_;
    }

private:
    std::int32_t value_ = 0;
    std::vector<StepEffect> effects_;
};

class Heard final : public EffectSink {
public:
    void deliver(const PredictedEffect& effect) noexcept override {
        delivered.push_back(effect);
    }
    void cancel(const PredictedEffect& effect) noexcept override {
        cancelled.push_back(effect);
    }
    std::vector<PredictedEffect> delivered;
    std::vector<PredictedEffect> cancelled;
};

std::vector<std::uint64_t> ticksOf(const std::vector<PredictedEffect>& effects, std::uint32_t kind) {
    std::vector<std::uint64_t> ticks;
    for (const PredictedEffect& effect : effects) {
        if (effect.kind == kind) {
            ticks.push_back(effect.tick);
        }
    }
    return ticks;
}

struct Run {
    Counting predictor;
    Heard heard;
    Prediction prediction{PredictionSettings{.predictor = &predictor,
                                             .predicted = {kCounter},
                                             .effects = &heard,
                                             .effectClasses = {EffectClass::Predicted, EffectClass::ConfirmedOnly}},
                          sizeof(std::int32_t)};

    /// Ones for input ticks 1 to 10, predicted from nought at tick 0.
    Run() {
        const std::int32_t kOne = 1;
        for (std::uint64_t tick = 1; tick <= 10; ++tick) {
            prediction.command(tick, std::as_bytes(std::span{&kOne, 1}));
        }
        server(0, 0);
    }
    bool server(std::uint64_t consumed, std::int32_t value) {
        const std::array<std::span<const std::byte>, 1> kValues = {std::as_bytes(std::span{&value, 1})};
        return prediction.authoritative(consumed, kValues);
    }
};

} // namespace

RAWFRAME_TEST(PredictedEffectsAreDeliveredOnceAndConfirmedOnesWait) {
    Run run;
    // Counting 1 to 10: fives at ticks 5 and 10; threes wait.
    RAWFRAME_EXPECT((ticksOf(run.heard.delivered, 0) == std::vector<std::uint64_t>{5, 10}));
    RAWFRAME_EXPECT(ticksOf(run.heard.delivered, 1).empty());
    // The server confirms tick 6: the threes at 3 and 6 are released.
    RAWFRAME_EXPECT(run.server(6, 6));
    RAWFRAME_EXPECT((ticksOf(run.heard.delivered, 1) == std::vector<std::uint64_t>{3, 6}));
    RAWFRAME_EXPECT(run.heard.cancelled.empty());
}

RAWFRAME_TEST(AResimulationThatEmitsTheSameEffectsDeliversNothingNew) {
    Run run;
    // The server was five ahead at tick 3: every multiple of five falls on
    // the same ticks, so nothing is delivered or cancelled again.
    RAWFRAME_EXPECT(!run.server(3, 8));
    RAWFRAME_EXPECT((ticksOf(run.heard.delivered, 0) == std::vector<std::uint64_t>{5, 10}));
    RAWFRAME_EXPECT(run.heard.cancelled.empty());
    RAWFRAME_EXPECT(run.prediction.statistics().effectsSuppressed == 2);
}

RAWFRAME_TEST(AResimulationThatEmitsOthersCancelsWhatItNoLongerEmits) {
    Run run;
    // The server was one ahead at tick 3: fives now fall at 4 and 9, so 5
    // and 10 are cancelled and 4 and 9 delivered; the three at tick 3 was
    // never confirmed and is dropped, never delivered.
    RAWFRAME_EXPECT(!run.server(3, 4));
    RAWFRAME_EXPECT((ticksOf(run.heard.delivered, 0) == std::vector<std::uint64_t>{5, 10, 4, 9}));
    RAWFRAME_EXPECT((ticksOf(run.heard.cancelled, 0) == std::vector<std::uint64_t>{5, 10}));
    RAWFRAME_EXPECT(ticksOf(run.heard.cancelled, 1).empty());
    // Confirmed at 10 (the counter at 11): the threes the resimulation
    // emitted, at 5 (6) and 8 (9), are released; none before tick 4.
    RAWFRAME_EXPECT(run.server(10, 11));
    RAWFRAME_EXPECT((ticksOf(run.heard.delivered, 1) == std::vector<std::uint64_t>{5, 8}));
}
