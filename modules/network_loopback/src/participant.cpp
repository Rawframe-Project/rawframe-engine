#include "rawframe/composition/composition.h"
#include "rawframe/network/conditions.h"
#include "rawframe/network/errors.h"
#include "rawframe/network/transport.h"
#include "rawframe/network_loopback/loopback.h"
#include "rawframe/network_loopback/registrar.h"

#include <limits>

namespace rawframe::network_loopback {

namespace {

constexpr std::string_view kProvided[] = {network::kTransport.name};

class LoopbackTransport final : public composition::Participant, public network::Transport {
public:
    LoopbackTransport(const execution::MonotonicSource& clock, network::LinkConditions conditions)
        : network_(clock, conditions) {
    }

    result::Result<std::unique_ptr<network::Provider>> provider(const network::ProviderProfile& profile) override {
        return network_.provider(profile);
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == network::kTransport.name) {
            return composition::provideAs<network::Transport>(*this);
        }
        return {};
    }

private:
    LoopbackNetwork network_;
};

result::Result<composition::ParticipantOwner> makeLoopback(composition::ParticipantContext& context) noexcept {
    const composition::Configuration& configuration = context.configuration();
    RAWFRAME_TRY_ASSIGN(const network::LinkConditions kConditions,
                        network::conditionsOf(configuration, "network.loopback"));
    return composition::ParticipantOwner{new LoopbackTransport{context.clock(), kConditions}};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.network.loopback",
        .factory = &makeLoopback,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvided,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "network.loopback",
    });
}

} // namespace rawframe::network_loopback
