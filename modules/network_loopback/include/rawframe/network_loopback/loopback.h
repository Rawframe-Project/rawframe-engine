#pragma once

// An in-process network for tests and single-process play (SPEC-0010
// loopback). Providers made from one LoopbackNetwork reach each other's
// listeners by name. Every byte is copied at send and owned by the receiver's
// queue; nothing is shared between peers. Delivery follows the network's
// clock and seeded conditions, so a run with one seed and one sequence of
// calls delivers the same way every time.

#include "rawframe/execution/time.h"
#include "rawframe/network/provider.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>

namespace rawframe::network_loopback {

/// What the network does to traffic. Streams keep their order and lose
/// nothing; datagrams may be delayed unevenly, lost, and duplicated.
struct LoopbackConditions {
    /// One way, for everything.
    execution::MonotonicDuration latency;
    /// Datagrams only: up to this much more, drawn per datagram, which is
    /// what reorders them.
    execution::MonotonicDuration jitter;
    std::uint32_t datagramLossPerMillion = 0;
    std::uint32_t datagramDuplicatePerMillion = 0;
    std::uint64_t seed = 0;
};

class LoopbackNetwork {
public:
    /// `clock` decides when sent bytes arrive and must outlive the network.
    LoopbackNetwork(const execution::MonotonicSource& clock, LoopbackConditions conditions);
    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;
    /// Every provider must be gone first.
    ~LoopbackNetwork();

    /// A provider on this network. Refuses (`invalid_argument`) a profile
    /// with any bound left at zero. Safe to use from several threads; each
    /// provider is one Runtime's.
    [[nodiscard]] result::Result<std::unique_ptr<network::Provider>> provider(const network::ProviderProfile& profile);

    struct State;

private:
    std::shared_ptr<State> state_;
};

} // namespace rawframe::network_loopback
