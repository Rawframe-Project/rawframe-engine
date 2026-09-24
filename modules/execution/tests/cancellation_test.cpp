// The cancellation tree: direction, reasons, failure policy, deadlines,
// callbacks, and the depth ceiling.

#include "rawframe/execution/bounds.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/errors.h"
#include "rawframe/test/test.h"

using namespace rawframe::execution;

RAWFRAME_TEST(CancellationFlowsDownNeverUp) {
    ManualClock clock;
    CancellationScope root{clock};
    auto left = CancellationScope::child(root);
    auto right = CancellationScope::child(root);
    auto leaf = CancellationScope::child(*left);
    left->cancel(CancelReason::Requested);
    RAWFRAME_EXPECT(left->cancelled());
    RAWFRAME_EXPECT(leaf->cancelled());
    RAWFRAME_EXPECT(leaf->reason() == CancelReason::Requested);
    RAWFRAME_EXPECT(!root.cancelled());
    RAWFRAME_EXPECT(!right->cancelled());
}

RAWFRAME_TEST(TheFirstReasonWinsAndRepeatsDoNothing) {
    ManualClock clock;
    CancellationScope root{clock};
    auto child = CancellationScope::child(root);
    root.cancel(CancelReason::OwnerStopping);
    root.cancel(CancelReason::Requested);
    child->cancel(CancelReason::Superseded);
    RAWFRAME_EXPECT(root.reason() == CancelReason::OwnerStopping);
    RAWFRAME_EXPECT(child->reason() == CancelReason::OwnerStopping);
}

RAWFRAME_TEST(AChildOfACancelledScopeStartsCancelled) {
    ManualClock clock;
    CancellationScope root{clock};
    root.cancel(CancelReason::PeerDisconnected);
    auto child = CancellationScope::child(root);
    RAWFRAME_EXPECT(child->reason() == CancelReason::PeerDisconnected);
}

RAWFRAME_TEST(FailurePolicyDecidesWhetherSiblingsStop) {
    ManualClock clock;
    CancellationScope root{clock};
    {
        auto isolated = CancellationScope::child(root, FailurePolicy::Isolate);
        auto sibling = CancellationScope::child(*isolated);
        isolated->reportFailure();
        RAWFRAME_EXPECT(!isolated->cancelled());
        RAWFRAME_EXPECT(!sibling->cancelled());
    }
    auto failFast = CancellationScope::child(root, FailurePolicy::FailFast);
    auto sibling = CancellationScope::child(*failFast);
    failFast->reportFailure();
    RAWFRAME_EXPECT(sibling->reason() == CancelReason::ParentFailed);
    RAWFRAME_EXPECT(!root.cancelled());
}

RAWFRAME_TEST(DeadlinesFollowTheMonotonicClock) {
    ManualClock clock{MonotonicInstant{1000}};
    CancellationScope root{clock};
    auto operation = CancellationScope::child(root);
    auto sibling = CancellationScope::child(root);
    auto step = CancellationScope::child(*operation);
    operation->setDeadline(clock.now() + MonotonicDuration::fromMilliseconds(10));
    // A later deadline never extends an earlier one.
    operation->setDeadline(clock.now() + MonotonicDuration::fromSeconds(10));

    clock.advance(MonotonicDuration::fromMilliseconds(9));
    RAWFRAME_EXPECT(!step->cancelled());
    clock.advance(MonotonicDuration::fromMilliseconds(1));
    // Noticed from below, the expiry cancels the scope that owns the deadline.
    RAWFRAME_EXPECT(step->cancelled());
    RAWFRAME_EXPECT(operation->reason() == CancelReason::DeadlineReached);
    RAWFRAME_EXPECT(step->reason() == CancelReason::DeadlineReached);
    RAWFRAME_EXPECT(!sibling->cancelled());
    RAWFRAME_EXPECT(!root.cancelled());
}

namespace {

void countCalls(void* context, CancelReason) noexcept {
    ++*static_cast<int*>(context);
}

} // namespace

RAWFRAME_TEST(CallbacksRunOnceAndRevokeOnDestruction) {
    ManualClock clock;
    CancellationScope root{clock};
    int calls = 0;
    int revokedCalls = 0;
    CancellationCallback kept{root, &countCalls, &calls};
    {
        const CancellationCallback kRevoked{root, &countCalls, &revokedCalls};
    }
    root.cancel(CancelReason::Requested);
    root.cancel(CancelReason::Requested);
    RAWFRAME_EXPECT(calls == 1);
    RAWFRAME_EXPECT(revokedCalls == 0);

    int late = 0;
    const CancellationCallback kLate{root, &countCalls, &late};
    RAWFRAME_EXPECT(late == 1);
}

RAWFRAME_TEST(ADefaultTokenIsNeverCancelled) {
    const CancellationToken kToken;
    RAWFRAME_EXPECT(!kToken.cancelled());
    RAWFRAME_EXPECT(!kToken.reason().has_value());
}

namespace {

/// Nests children until creation fails, and returns the depth reached.
std::size_t nestUntilRefused(CancellationScope& parent) {
    auto child = CancellationScope::child(parent);
    if (!child.has_value()) {
        RAWFRAME_EXPECT(child.error().errorClass() == rawframe::result::ErrorClass::ResourceExhausted);
        RAWFRAME_EXPECT(child.error().code() == code(ExecutionError::ScopeTooDeep));
        return parent.depth();
    }
    return nestUntilRefused(*child);
}

} // namespace

RAWFRAME_TEST(ScopeDepthIsRefusedAtExactlyItsBound) {
    ManualClock clock;
    CancellationScope root{clock};
    RAWFRAME_EXPECT(root.depth() == 1);
    RAWFRAME_EXPECT(nestUntilRefused(root) == kMaximumCancellationScopeDepth);
}
