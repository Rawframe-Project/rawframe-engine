#include "rawframe/composition/host_lifecycle.h"

namespace rawframe::composition {

std::string_view describe(HostState state) noexcept {
    switch (state) {
    case HostState::Starting:
        return "starting";
    case HostState::Preparing:
        return "preparing";
    case HostState::Ready:
        return "ready";
    case HostState::Active:
        return "active";
    case HostState::Draining:
        return "draining";
    case HostState::Stopping:
        return "stopping";
    case HostState::Stopped:
        return "stopped";
    case HostState::Failed:
        return "failed";
    }
    return "failed";
}

std::string_view describe(Health health) noexcept {
    switch (health) {
    case Health::Healthy:
        return "healthy";
    case Health::Degraded:
        return "degraded";
    case Health::Unhealthy:
        return "unhealthy";
    }
    return "unhealthy";
}

bool allowed(HostState from, HostState to) noexcept {
    switch (from) {
    case HostState::Starting:
        return to == HostState::Preparing || to == HostState::Failed;
    case HostState::Preparing:
        return to == HostState::Ready || to == HostState::Failed;
    case HostState::Ready:
        return to == HostState::Active || to == HostState::Draining || to == HostState::Failed;
    case HostState::Active:
        return to == HostState::Draining || to == HostState::Failed;
    case HostState::Draining:
        return to == HostState::Stopping;
    case HostState::Stopping:
        return to == HostState::Stopped;
    case HostState::Stopped:
    case HostState::Failed:
        return false;
    }
    return false;
}

bool HostLifecycle::enter(HostState next) noexcept {
    const HostState kCurrent = state();
    if (!allowed(kCurrent, next)) {
        return false;
    }
    state_.store(next, std::memory_order_release);
    return true;
}

} // namespace rawframe::composition
