#include "present.h"

#include <array>
#include <span>

namespace rawframe::render {

namespace {

/// A policy's present modes, first choice first; fifo ends every chain.
std::span<const mrhiPresentMode> chainOf(PresentPolicy policy) noexcept {
    static constexpr std::array<mrhiPresentMode, 1> kVsync = {mrhi_presentFifo};
    static constexpr std::array<mrhiPresentMode, 2> kLowLatency = {mrhi_presentMailbox, mrhi_presentFifo};
    static constexpr std::array<mrhiPresentMode, 3> kImmediate = {
        mrhi_presentImmediate, mrhi_presentMailbox, mrhi_presentFifo};
    switch (policy) {
    case PresentPolicy::LowLatencyVsync:
        return kLowLatency;
    case PresentPolicy::Immediate:
        return kImmediate;
    case PresentPolicy::Vsync:
    case PresentPolicy::AdaptiveVsync:
        // Maul RHI has no relaxed fifo: adaptive vsync is vsync.
        break;
    }
    return kVsync;
}

} // namespace

mrhiPresentMode presentModeFor(PresentPolicy policy, mrhiPresentModes offered) noexcept {
    // A set holds a mode as the bit its value shifts 1 by; the mode is
    // the value, never the bit.
    for (const mrhiPresentMode kMode : chainOf(policy)) {
        if ((offered & (mrhiPresentModes{1} << kMode)) != 0) {
            return kMode;
        }
    }
    return mrhi_presentFifo;
}

PresentPolicy policyOf(mrhiPresentMode mode) noexcept {
    switch (mode) {
    case mrhi_presentMailbox:
        return PresentPolicy::LowLatencyVsync;
    case mrhi_presentImmediate:
        return PresentPolicy::Immediate;
    default:
        return PresentPolicy::Vsync;
    }
}

} // namespace rawframe::render
