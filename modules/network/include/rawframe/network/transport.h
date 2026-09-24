#pragma once

#include "rawframe/composition/participant.h"
#include "rawframe/network/provider.h"
#include "rawframe/result/result.h"

#include <memory>

namespace rawframe::network {

/// Where providers come from in a composition: the loopback network, or a
/// QUIC driver. Whoever needs to listen or connect asks it for a provider of
/// its own, with its own bounds.
class Transport {
public:
    Transport() = default;
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    virtual ~Transport() = default;

    [[nodiscard]] virtual result::Result<std::unique_ptr<Provider>> provider(const ProviderProfile& profile) = 0;
};

inline constexpr composition::Capability<Transport> kTransport{"rawframe.network.transport"};

} // namespace rawframe::network
