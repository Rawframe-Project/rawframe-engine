#include "rawframe/composition/composition.h"
#include "rawframe/network/transport.h"
#include "rawframe/network_web/registrar.h"
#include "rawframe/network_web/web.h"

namespace rawframe::network_web {

namespace {

constexpr std::string_view kProvided[] = {network::kTransport.name};

class WebTransport final : public composition::Participant, public network::Transport {
public:
    result::Result<std::unique_ptr<network::Provider>> provider(const network::ProviderProfile& profile) override {
        return webProvider(profile);
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == network::kTransport.name) {
            return composition::provideAs<network::Transport>(*this);
        }
        return {};
    }
};

result::Result<composition::ParticipantOwner> makeWeb(composition::ParticipantContext&) noexcept {
    return composition::ParticipantOwner{new WebTransport{}};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.network.web",
        .factory = &makeWeb,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvided,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "network.web",
    });
}

} // namespace rawframe::network_web
