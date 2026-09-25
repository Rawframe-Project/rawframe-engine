#include "prediction.h"

#include <algorithm>

namespace rawframe::world_replication {

Prediction::Prediction(PredictionSettings settings, std::size_t inputSize)
    : settings_(std::move(settings)), inputSize_(inputSize), known_(settings_.predicted.size()) {
}

void Prediction::reset() {
    commands_.clear();
    history_.clear();
    known_.assign(settings_.predicted.size(), {});
    started_ = false;
    predictedTick_ = 0;
    newestCommand_ = 0;
}

std::vector<std::byte> Prediction::commandFor(std::uint64_t tick) const {
    const auto kAfter = commands_.upper_bound(tick);
    if (kAfter != commands_.begin()) {
        const auto kAt = std::prev(kAfter);
        if (kAt->first == tick || tick - kAt->first <= settings_.holdLast) {
            return kAt->second;
        }
    }
    return std::vector<std::byte>(inputSize_, std::byte{0});
}

Prediction::State Prediction::read() const {
    State state;
    for (const schema::ComponentTypeId kComponent : settings_.predicted) {
        const auto kValue = settings_.predictor->get(kComponent);
        state.emplace_back(kValue.begin(), kValue.end());
    }
    return state;
}

void Prediction::write(const State& state) {
    for (std::size_t index = 0; index < state.size(); ++index) {
        static_cast<void>(settings_.predictor->set(settings_.predicted[index], state[index]));
    }
}

void Prediction::advance(std::uint64_t through) {
    while (predictedTick_ < through) {
        if (history_.size() >= settings_.window) {
            ++statistics_.stalled;
            return;
        }
        const std::uint64_t kTick = predictedTick_ + 1;
        if (!settings_.predictor->step(commandFor(kTick)).has_value()) {
            ++statistics_.failedSteps;
            return;
        }
        predictedTick_ = kTick;
        history_[kTick] = read();
        ++statistics_.predictedTicks;
    }
}

void Prediction::command(std::uint64_t tick, std::span<const std::byte> value) {
    commands_[tick].assign(value.begin(), value.end());
    newestCommand_ = std::max(newestCommand_, tick);
    if (started_) {
        advance(newestCommand_);
    }
}

void Prediction::authoritative(std::uint64_t consumed, std::span<const std::span<const std::byte>> values) {
    for (std::size_t index = 0; index < values.size() && index < known_.size(); ++index) {
        if (!values[index].empty()) {
            known_[index].assign(values[index].begin(), values[index].end());
        }
    }
    if (!started_) {
        // Start once every predicted component has been heard of.
        if (std::ranges::any_of(known_, [](const std::vector<std::byte>& value) {
                return value.empty();
            })) {
            return;
        }
        write(known_);
        started_ = true;
        predictedTick_ = consumed;
        history_.clear();
        advance(newestCommand_);
        return;
    }
    const auto kPredicted = history_.find(consumed);
    if (kPredicted == history_.end() && consumed <= predictedTick_) {
        // Already compared and released, or older than the window: nothing
        // this state can say about what is predicted now.
        return;
    }
    bool equal = kPredicted != history_.end();
    State base = equal ? kPredicted->second : read();
    for (std::size_t index = 0; index < values.size() && index < base.size(); ++index) {
        if (!values[index].empty()) {
            equal = equal && std::ranges::equal(base[index], values[index]);
            base[index].assign(values[index].begin(), values[index].end());
        }
    }
    // Commands the server is done with are only needed to hold the last one.
    if (consumed > settings_.holdLast) {
        commands_.erase(commands_.begin(), commands_.lower_bound(consumed - settings_.holdLast));
    }
    if (equal) {
        ++statistics_.confirmed;
        history_.erase(history_.begin(), history_.upper_bound(consumed));
        return;
    }
    // Back to the server's state at `consumed`, then every later input again.
    ++statistics_.rollbacks;
    const std::uint64_t kThrough = std::max(predictedTick_, consumed);
    write(base);
    history_.clear();
    predictedTick_ = consumed;
    const std::uint64_t kBefore = statistics_.predictedTicks;
    advance(kThrough);
    statistics_.resimulatedTicks += statistics_.predictedTicks - kBefore;
    statistics_.predictedTicks = kBefore;
}

std::optional<std::span<const std::byte>> Prediction::current(std::size_t index) const {
    if (!started_ || index >= settings_.predicted.size()) {
        return std::nullopt;
    }
    return settings_.predictor->get(settings_.predicted[index]);
}

} // namespace rawframe::world_replication
