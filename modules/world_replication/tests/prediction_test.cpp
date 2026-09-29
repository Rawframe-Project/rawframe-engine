// What a confirmation confirms (D274): a state that carries only some of the
// player's values confirms those alone, unless the server says it is whole,
// when a value it leaves out is the one last heard and a prediction that
// strayed from it is rolled back, not taken for the server's.

#include "../src/prediction.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstring>

using namespace rawframe;
using namespace rawframe::world_replication;

namespace {

constexpr schema::ComponentTypeId kCount = schema::ComponentTypeId::fromText("3e8a1c5d-7b20-4f96-a4d1-0c6e9b2f5a17");
constexpr schema::ComponentTypeId kMark = schema::ComponentTypeId::fromText("9b4f2e6a-1d73-4c58-8e0a-5f2d7c1b3e94");

/// A count each step adds its input to, and a mark the server never moves,
/// which a faulty predictor moves each step.
class Marking final : public Predictor {
public:
    explicit Marking(bool faulty) : faulty_(faulty) {
    }
    std::span<const std::byte> get(schema::ComponentTypeId component) const noexcept override {
        return std::as_bytes(std::span{component == kCount ? &count_ : &mark_, 1});
    }
    result::Status set(schema::ComponentTypeId component, std::span<const std::byte> value) override {
        std::memcpy(component == kCount ? &count_ : &mark_, value.data(), sizeof count_);
        return {};
    }
    result::Status step(std::span<const std::byte> input) override {
        std::int32_t add = 0;
        std::memcpy(&add, input.data(), sizeof add);
        count_ += add;
        mark_ += faulty_ ? 1 : 0;
        return {};
    }
    void rate(world::TickRate) noexcept override {
    }
    result::Status place(std::span<const NeighborValue>) override {
        return {};
    }

private:
    bool faulty_;
    std::int32_t count_ = 0;
    std::int32_t mark_ = 0;
};

std::span<const std::byte> bytesOf(const std::int32_t& value) {
    return std::as_bytes(std::span{&value, 1});
}

struct Run {
    Marking predictor;
    Prediction prediction{PredictionSettings{.predictor = &predictor, .predicted = {kCount, kMark}},
                          sizeof(std::int32_t)};

    /// Ones for input ticks 1 to 10, predicted from noughts at tick 0.
    explicit Run(bool faulty) : predictor(faulty) {
        const std::int32_t kOne = 1;
        for (std::uint64_t tick = 1; tick <= 10; ++tick) {
            prediction.command(tick, bytesOf(kOne));
        }
        const std::int32_t kNought = 0;
        const std::array<std::span<const std::byte>, 2> kStart = {bytesOf(kNought), bytesOf(kNought)};
        static_cast<void>(prediction.authoritative(0, kStart, true));
    }

    /// The server's state after input 3: its count only, the mark left out.
    bool countOnly(bool whole) {
        const std::int32_t kThree = 3;
        const std::array<std::span<const std::byte>, 2> kValues = {bytesOf(kThree), {}};
        return prediction.authoritative(3, kValues, whole);
    }
};

std::int32_t valueOf(const std::vector<std::byte>& bytes) {
    std::int32_t value = 0;
    std::memcpy(&value, bytes.data(), sizeof value);
    return value;
}

} // namespace

RAWFRAME_TEST(AWholeStateComparesWhatItLeavesOut) {
    // Told only part, the stray mark cannot be seen: the count confirms.
    Run partial{true};
    RAWFRAME_EXPECT(partial.countOnly(false) && partial.prediction.statistics().rollbacks == 0);
    // Told whole, the mark is the nought last heard, and the prediction's
    // three is wrong: rolled back, not confirmed.
    Run whole{true};
    RAWFRAME_EXPECT(!whole.countOnly(true) && whole.prediction.statistics().rollbacks == 1);
    // A prediction that kept the mark is confirmed whole, and what is
    // confirmed is the server's state there, the mark as last heard.
    Run kept{false};
    RAWFRAME_EXPECT(kept.countOnly(true) && kept.prediction.statistics().rollbacks == 0);
    const auto& kConfirmed = kept.prediction.confirmed();
    RAWFRAME_EXPECT(kConfirmed.size() == 2 && valueOf(kConfirmed[0]) == 3 && valueOf(kConfirmed[1]) == 0);
}
