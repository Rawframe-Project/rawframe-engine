#include "rawframe/composition/composition.h"
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
    LoopbackTransport(const execution::MonotonicSource& clock, LoopbackConditions conditions)
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
    constexpr std::uint64_t kMaximumMilliseconds = 60'000;
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLatency, configuration.unsignedInteger("network.loopback.latency_ms", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kJitter, configuration.unsignedInteger("network.loopback.jitter_ms", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLoss, configuration.unsignedInteger("network.loopback.loss_ppm", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kDuplicate,
                        configuration.unsignedInteger("network.loopback.duplicate_ppm", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kSeed, configuration.unsignedInteger("network.loopback.seed", 0));
    if (kLatency > kMaximumMilliseconds || kJitter > kMaximumMilliseconds || kLoss > 1'000'000 ||
        kDuplicate > 1'000'000) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            network::kNetworkDomain,
                            network::code(network::NetworkError::InvalidProfile),
                            "a loopback condition is out of range");
    }
    const LoopbackConditions kConditions{
        .latency = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kLatency)),
        .jitter = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kJitter)),
        .datagramLossPerMillion = static_cast<std::uint32_t>(kLoss),
        .datagramDuplicatePerMillion = static_cast<std::uint32_t>(kDuplicate),
        .seed = kSeed};
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
