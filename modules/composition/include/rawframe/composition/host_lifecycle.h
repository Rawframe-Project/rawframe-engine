#pragma once

// SPEC-0012's host lifecycle and health. The Host owns one HostLifecycle and
// alone moves it; participants read it through their context (admission is
// open only while active) and report the evidence the Host reads back: the
// connections they still serve, which end a drain early, and their health.

#include <atomic>
#include <cstdint>
#include <string_view>

namespace rawframe::composition {

/// `starting -> preparing -> ready -> active -> draining -> stopping ->
/// stopped`, and `failed` from any state before draining. Nothing returns to
/// an earlier state.
enum class HostState : std::uint8_t {
    Starting,
    Preparing,
    Ready,
    Active,
    Draining,
    Stopping,
    Stopped,
    Failed,
};

/// Distinct from the lifecycle: a healthy Host may be draining, and an
/// unhealthy one is drained and stopped. Ordered, so the worst is the
/// greatest.
enum class Health : std::uint8_t {
    Healthy,
    Degraded,
    Unhealthy,
};

[[nodiscard]] std::string_view describe(HostState state) noexcept;
[[nodiscard]] std::string_view describe(Health health) noexcept;

/// Whether SPEC-0012 allows `from -> to`.
[[nodiscard]] bool allowed(HostState from, HostState to) noexcept;

class HostLifecycle {
public:
    /// Readable from any thread.
    [[nodiscard]] HostState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    /// Gameplay admission: open only while active and not degraded
    /// (SPEC-0012 lets degraded close it; SPEC-0013's canonical profile
    /// does, D212), so it reopens once the Host is healthy again.
    [[nodiscard]] bool admitting() const noexcept {
        return state() == HostState::Active && !degraded_.load(std::memory_order_acquire);
    }

    /// The Host's health, as its thread last judged it.
    void judge(Health health) noexcept {
        degraded_.store(health != Health::Healthy, std::memory_order_release);
    }

    /// Moves to `next` if allowed from the current state; the Host's thread
    /// only. Returns whether it moved.
    [[nodiscard]] bool enter(HostState next) noexcept;

private:
    std::atomic<HostState> state_{HostState::Starting};
    std::atomic<bool> degraded_{false};
};

} // namespace rawframe::composition
