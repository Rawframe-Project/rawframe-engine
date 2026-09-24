#include "rawframe/diagnostics/router.h"

#include "rawframe/diagnostics/emitter.h"

#include <chrono>

namespace rawframe::diagnostics {

std::uint64_t steadyClockNanoseconds() noexcept {
    const auto kSinceEpoch = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(kSinceEpoch).count());
}

namespace {

/// Spreads a counter over 64 bits so identities are not visibly sequential.
std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value;
}

} // namespace

Router::Router(RouterSettings settings, std::span<Sink* const> sinks) noexcept : settings_(settings) {
    if (settings_.monotonic == nullptr) {
        settings_.monotonic = &steadyClockNanoseconds;
    }
    for (Sink* sink : sinks) {
        if (sink != nullptr && sinkCount_ < kMaximumSinks) {
            sinks_[sinkCount_++] = sink;
        }
    }
}

Emitter Router::emitter() noexcept {
    return Emitter{*this};
}

std::uint64_t Router::now() const noexcept {
    return settings_.monotonic();
}

void Router::deliver(Record& record) noexcept {
    if (stopped_.load(std::memory_order_acquire)) {
        droppedAfterStop_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    record.time.monotonicNanoseconds = settings_.monotonic();
    if (settings_.wall != nullptr) {
        record.time.wallUnixNanoseconds = settings_.wall();
    }
    for (std::size_t index = 0; index < sinkCount_; ++index) {
        sinks_[index]->accept(record);
    }
}

void Router::stop() noexcept {
    stopped_.store(true, std::memory_order_release);
}

base::Bits128 Router::newTrace() noexcept {
    // Two splitmix outputs from one state, so neither half is zero for a zero
    // seed or a small counter.
    constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ULL;
    const std::uint64_t kState = settings_.traceSeed + kGolden * nextIdentity_.fetch_add(1, std::memory_order_relaxed);
    return base::Bits128{mix(kState), mix(kState + kGolden)};
}

std::uint64_t Router::newSpan() noexcept {
    // Zero means "no span", so it is never handed out.
    const std::uint64_t kSpan = mix(nextIdentity_.fetch_add(1, std::memory_order_relaxed) ^ settings_.traceSeed);
    return kSpan == 0 ? 1 : kSpan;
}

} // namespace rawframe::diagnostics
