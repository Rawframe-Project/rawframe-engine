#pragma once

// What a link does to traffic (D805), the one description loopback's
// network and any provider wrapped in conditions share: one-way latency,
// jitter, loss, and duplication, seeded so a run delivers the same way
// again. Streams keep their order and lose nothing; datagrams may be
// delayed unevenly, lost, and duplicated.

#include "rawframe/composition/configuration.h"
#include "rawframe/execution/time.h"
#include "rawframe/network/provider.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace rawframe::network {

struct LinkConditions {
    /// One way, for everything.
    execution::MonotonicDuration latency;
    /// Datagrams only: up to this much more, drawn per datagram, which is
    /// what reorders them.
    execution::MonotonicDuration jitter;
    std::uint32_t datagramLossPerMillion = 0;
    std::uint32_t datagramDuplicatePerMillion = 0;
    std::uint64_t seed = 0;

    /// Whether these do anything.
    [[nodiscard]] bool any() const noexcept {
        return latency.nanoseconds > 0 || jitter.nanoseconds > 0 || datagramLossPerMillion > 0 ||
               datagramDuplicatePerMillion > 0;
    }
};

/// `inner` with `conditions` on what it sends and what it receives, as a
/// slower link between it and its peers would be: each send held until the
/// latency has passed (a datagram's with its jitter, or lost, or twice),
/// then given to `inner`; each event `inner` receives held as long before it
/// is polled. So one side alone adds the latency both ways. Sends are
/// given on, and arrivals taken, when the provider is polled, which its
/// owner does every frame. What arrives is held up to the profile's queued
/// events, past which it waits in `inner`. `clock` must outlive it.
[[nodiscard]] std::unique_ptr<Provider> withConditions(std::unique_ptr<Provider> inner,
                                                       const LinkConditions& conditions,
                                                       const ProviderProfile& profile,
                                                       const execution::MonotonicSource& clock);

/// The conditions `<prefix>.latency_ms`, `.jitter_ms`, `.loss_ppm`,
/// `.duplicate_ppm`, and `.seed` set, each nought when unset: a transport's
/// own, `network.loopback` or `network.quic`. Refuses a latency or jitter
/// past a minute and a part past a million (`invalid_profile`).
[[nodiscard]] result::Result<LinkConditions> conditionsOf(const composition::Configuration& configuration,
                                                          std::string_view prefix);

} // namespace rawframe::network
