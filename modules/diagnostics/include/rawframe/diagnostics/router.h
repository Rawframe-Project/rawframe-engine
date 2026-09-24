#pragma once

#include "rawframe/diagnostics/record.h"
#include "rawframe/diagnostics/sink.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace rawframe::diagnostics {

class Emitter;

/// The most sinks one router delivers to.
inline constexpr std::size_t kMaximumSinks = 8;

/// Reads a clock in nanoseconds. Injected so that tests and a Host can own time.
using MonotonicClock = std::uint64_t (*)() noexcept;
using WallClock = std::int64_t (*)() noexcept;

/// The process steady clock in nanoseconds, the default MonotonicClock.
[[nodiscard]] std::uint64_t steadyClockNanoseconds() noexcept;

struct RouterSettings {
    /// Records below this severity are not delivered, and their emitters skip
    /// formatting entirely.
    Severity minimumSeverity = Severity::Info;
    MonotonicClock monotonic = nullptr; // null: the process steady clock
    WallClock wall = nullptr;           // null: records carry no wall time
    /// Mixed into trace identities so two routers never mint the same one.
    std::uint64_t traceSeed = 0;
};

/// Routes records to the sinks its Host gave it. Owned by the Host, constructed
/// with its sinks, and handed out only as Emitters: nothing looks it up. Because
/// the sinks exist before the router, there is no window in which a router
/// exists but cannot deliver, so no bootstrap buffer is needed.
class Router {
public:
    Router(RouterSettings settings, std::span<Sink* const> sinks) noexcept;
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
    ~Router() = default;

    /// An emitter for owners to hold. Copyable and cheap.
    [[nodiscard]] Emitter emitter() noexcept;

    /// Whether the filter passes this severity. It ignores stop on purpose: a
    /// record emitted after stop still reaches deliver, which counts it.
    [[nodiscard]] bool enabled(Severity severity) const noexcept {
        return severity >= settings_.minimumSeverity;
    }

    /// Stamps the record's time and hands it to every sink.
    void deliver(Record& record) noexcept;

    /// Ends delivery. Later records are counted and dropped. Draining and
    /// closing the sinks is the Host's job and happens after this.
    void stop() noexcept;

    [[nodiscard]] std::uint64_t droppedAfterStop() const noexcept {
        return droppedAfterStop_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] base::Bits128 newTrace() noexcept;
    [[nodiscard]] std::uint64_t newSpan() noexcept;
    [[nodiscard]] std::uint64_t now() const noexcept;

private:
    RouterSettings settings_;
    std::array<Sink*, kMaximumSinks> sinks_{};
    std::size_t sinkCount_ = 0;
    std::atomic<bool> stopped_{false};
    std::atomic<std::uint64_t> droppedAfterStop_{0};
    std::atomic<std::uint64_t> nextIdentity_{1};
};

} // namespace rawframe::diagnostics
