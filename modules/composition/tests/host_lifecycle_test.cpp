// The Host lifecycle's transitions: the one forward path, failure before
// draining, nothing back, and admission open only while active.

#include "rawframe/composition/host_lifecycle.h"
#include "rawframe/test/test.h"

#include <array>

using namespace rawframe::composition;

namespace {

constexpr std::array<HostState, 8> kStates = {HostState::Starting,
                                              HostState::Preparing,
                                              HostState::Ready,
                                              HostState::Active,
                                              HostState::Draining,
                                              HostState::Stopping,
                                              HostState::Stopped,
                                              HostState::Failed};

} // namespace

RAWFRAME_TEST(ADegradedHostClosesAdmissionUntilHealthy) {
    // SPEC-0013's degraded action (D212): admission closes, and reopens
    // once the Host is healthy again.
    HostLifecycle lifecycle;
    for (std::size_t index = 1; index < 4; ++index) {
        RAWFRAME_EXPECT(lifecycle.enter(kStates[index]));
    }
    RAWFRAME_EXPECT(lifecycle.state() == HostState::Active && lifecycle.admitting());
    lifecycle.judge(Health::Degraded);
    RAWFRAME_EXPECT(lifecycle.state() == HostState::Active && !lifecycle.admitting());
    lifecycle.judge(Health::Healthy);
    RAWFRAME_EXPECT(lifecycle.admitting());
}

RAWFRAME_TEST(AHostLifecycleMovesOnlyForward) {
    HostLifecycle lifecycle;
    RAWFRAME_EXPECT(lifecycle.state() == HostState::Starting && !lifecycle.admitting());
    // Skipping a state is refused.
    RAWFRAME_EXPECT(!lifecycle.enter(HostState::Ready));
    for (std::size_t index = 1; index < 7; ++index) {
        RAWFRAME_EXPECT(lifecycle.enter(kStates[index]));
        RAWFRAME_EXPECT(lifecycle.admitting() == (kStates[index] == HostState::Active));
    }
    // Stopped is the end: nothing follows, nothing earlier returns.
    for (const HostState kNext : kStates) {
        RAWFRAME_EXPECT(!lifecycle.enter(kNext));
    }

    // Every state before draining may fail; draining and after may not,
    // and failed is the end too.
    for (const HostState kFrom : kStates) {
        const bool kCanFail = kFrom == HostState::Starting || kFrom == HostState::Preparing ||
                              kFrom == HostState::Ready || kFrom == HostState::Active;
        RAWFRAME_EXPECT(allowed(kFrom, HostState::Failed) == kCanFail);
        RAWFRAME_EXPECT(!allowed(HostState::Failed, kFrom));
        // No state returns to itself or to one before it.
        for (const HostState kTo : kStates) {
            if (kTo != HostState::Failed && kTo <= kFrom) {
                RAWFRAME_EXPECT(!allowed(kFrom, kTo));
            }
        }
    }
    // Ready may drain without having been active.
    RAWFRAME_EXPECT(allowed(HostState::Ready, HostState::Draining));
    RAWFRAME_EXPECT(describe(HostState::Draining) == "draining" && describe(Health::Degraded) == "degraded");
}
