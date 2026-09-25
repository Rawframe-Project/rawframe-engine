#include "interpolation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rawframe::world_replication {

namespace {

/// How far back the server tick estimate looks for its least delayed state.
constexpr std::int64_t kOffsetWindowNanoseconds = 2'000'000'000;

} // namespace

Interpolation::Interpolation(const InterpolationSettings& settings,
                             std::span<const ComponentCodec> table,
                             std::vector<bool> interpolated)
    : settings_(settings), table_(table.begin(), table.end()), interpolated_(std::move(interpolated)) {
}

void Interpolation::admitted(std::uint64_t ticks, std::uint64_t seconds) noexcept {
    ticksPerNanosecond_ = seconds == 0 ? 0 : static_cast<double>(ticks) / (static_cast<double>(seconds) * 1e9);
}

double Interpolation::nowTicks() const noexcept {
    return static_cast<double>(settings_.clock->now().nanoseconds) * ticksPerNanosecond_;
}

void Interpolation::heard(std::uint64_t serverTick) {
    const std::int64_t kNow = settings_.clock->now().nanoseconds;
    const double kOffset = static_cast<double>(serverTick) - nowTicks();
    while (!offsets_.empty() && offsets_.back().second <= kOffset) {
        offsets_.pop_back();
    }
    offsets_.emplace_back(kNow, kOffset);
    while (offsets_.front().first < kNow - kOffsetWindowNanoseconds) {
        offsets_.pop_front();
    }
}

void Interpolation::sample(std::uint32_t net,
                           std::size_t component,
                           std::uint64_t tick,
                           std::span<const std::byte> value) {
    std::deque<State>& states = states_[{net, component}];
    // States arrive in tick order but for loss and reordering; one older
    // than the newest kept was already passed over.
    if (!states.empty() && states.back().tick >= tick) {
        return;
    }
    states.push_back(State{.tick = tick, .value = {value.begin(), value.end()}});
    while (states.size() > settings_.samples) {
        states.pop_front();
    }
}

std::span<const std::byte> Interpolation::newest(std::uint32_t net, std::size_t component) const noexcept {
    const auto kStates = states_.find({net, component});
    return kStates == states_.end() || kStates->second.empty()
               ? std::span<const std::byte>{}
               : std::span<const std::byte>{kStates->second.back().value};
}

void Interpolation::retire(std::uint32_t net) {
    states_.erase(states_.lower_bound({net, 0}), states_.lower_bound({net + 1, 0}));
}

void Interpolation::reset() noexcept {
    offsets_.clear();
    states_.clear();
}

std::optional<double> Interpolation::perceivedTick() const noexcept {
    if (offsets_.empty() || ticksPerNanosecond_ == 0) {
        return std::nullopt;
    }
    return nowTicks() + offsets_.front().second - static_cast<double>(settings_.delay);
}

void Interpolation::blend(const ComponentCodec& codec, const State& earlier, const State& later, double fraction) {
    shown_ = earlier.value;
    for (const WireField& field : codec.fields) {
        if (field.kind == WireKind::F32) {
            float from = 0;
            float to = 0;
            std::memcpy(&from, earlier.value.data() + field.offset, sizeof from);
            std::memcpy(&to, later.value.data() + field.offset, sizeof to);
            const auto kValue = static_cast<float>(from + ((static_cast<double>(to) - from) * fraction));
            std::memcpy(shown_.data() + field.offset, &kValue, sizeof kValue);
        } else if (field.kind == WireKind::F64) {
            double from = 0;
            double to = 0;
            std::memcpy(&from, earlier.value.data() + field.offset, sizeof from);
            std::memcpy(&to, later.value.data() + field.offset, sizeof to);
            const double kValue = from + ((to - from) * fraction);
            std::memcpy(shown_.data() + field.offset, &kValue, sizeof kValue);
        }
    }
}

void Interpolation::show(world::World& world,
                         const std::map<std::uint32_t, world::EntityHandle>& mirrored,
                         std::span<const schema::ComponentRuntimeId> table) {
    const std::optional<double> kPerceived = perceivedTick();
    if (!kPerceived.has_value()) {
        return;
    }
    const double kAt = *kPerceived;
    // Both by ID: one walk along the mirrored entities.
    auto entity = mirrored.begin();
    for (auto& [key, states] : states_) {
        while (entity != mirrored.end() && entity->first < key.first) {
            ++entity;
        }
        if (entity == mirrored.end() || entity->first != key.first || states.empty()) {
            continue;
        }
        const ComponentCodec& codec = table_[key.second];
        // The newest state at or before the moment, and the one after it.
        std::size_t earlier = 0;
        while (earlier + 1 < states.size() && static_cast<double>(states[earlier + 1].tick) <= kAt) {
            ++earlier;
        }
        // What is older than that is never shown again.
        states.erase(states.begin(), states.begin() + static_cast<std::ptrdiff_t>(earlier));
        const State& from = states.front();
        if (states.size() == 1 || static_cast<double>(from.tick) > kAt) {
            // Past the newest, or before the first: that state as it is.
            shown_ = from.value;
            ++statistics_.newest;
        } else {
            const State& to = states[1];
            const double kStart = std::max(static_cast<double>(from.tick),
                                           static_cast<double>(to.tick) - static_cast<double>(settings_.maximumSpan));
            const double kFraction = kAt <= kStart ? 0 : (kAt - kStart) / (static_cast<double>(to.tick) - kStart);
            blend(codec, from, to, std::clamp(kFraction, 0.0, 1.0));
            ++statistics_.blended;
        }
        void* const kInto = world.getErased(entity->second, table[key.second]);
        if (kInto != nullptr) {
            std::memcpy(kInto, shown_.data(), codec.size);
        } else {
            static_cast<void>(world.insertErased(entity->second, table[key.second], shown_.data()));
        }
    }
}

} // namespace rawframe::world_replication
