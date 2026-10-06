#include "rawframe/composition/composition.h"
#include "rawframe/network/transport.h"
#include "rawframe/world_runtime/component_fields.h"
#include "rawframe/world_runtime/simulation.h"
#include "rawframe/world_tooling/errors.h"
#include "rawframe/world_tooling/registrar.h"
#include "rawframe/world_tooling/server.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace rawframe::world_tooling {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kListening{"tooling", "listening"};
constexpr EventIdentity kSummary{"tooling", "summary"};

constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kMaybe[] = {network::kTransport.name, world_runtime::kComponentFields.name};

/// A token file's bytes at most.
constexpr std::size_t kMaximumTokenBytes = 4096;

std::unexpected<result::Error> misconfigured(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kToolingDomain, code(ToolingError::Configuration), why);
}

/// The token a file holds: its first line, without the line feed.
result::Result<std::string> tokenFrom(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return misconfigured("tooling.token_file cannot be opened");
    }
    std::string text(kMaximumTokenBytes + 1, '\0');
    const std::size_t kRead = std::fread(text.data(), 1, text.size(), file);
    const bool kFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (kFailed || kRead > kMaximumTokenBytes) {
        return misconfigured("tooling.token_file cannot be read, or is too large");
    }
    text.resize(kRead);
    text.resize(std::min(text.find('\n'), text.size()));
    if (!text.empty() && text.back() == '\r') {
        text.pop_back();
    }
    return text;
}

class EndpointParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context, std::string endpoint) {
        endpoint_ = std::move(endpoint);
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(world_runtime::kSimulation));
        if (!context.has(network::kTransport.name)) {
            return misconfigured("a tooling endpoint needs a transport");
        }
        const auto kTokenFile = context.configuration().path("tooling.token_file");
        if (!kTokenFile.has_value()) {
            return misconfigured("a tooling endpoint needs tooling.token_file");
        }
        ToolingSettings settings;
        RAWFRAME_TRY_ASSIGN(settings.token, tokenFrom(std::string{*kTokenFile}));
        const auto kGrants = context.configuration().text("tooling.grants");
        if (kGrants.has_value() && *kGrants != "inspect") {
            return misconfigured("tooling.grants is inspect: generation 1 grants nothing more");
        }
        settings.grants.inspect = true;
        if (context.has(world_runtime::kComponentFields.name)) {
            RAWFRAME_TRY_ASSIGN(settings.fields, context.capability(world_runtime::kComponentFields));
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kClients,
                            context.configuration().unsignedInteger("tooling.maximum_clients", 4));
        if (kClients == 0 || kClients > 16) {
            return misconfigured("tooling.maximum_clients is 1 to 16");
        }
        RAWFRAME_TRY_ASSIGN(network::Transport * transport, context.capability(network::kTransport));
        RAWFRAME_TRY_ASSIGN(provider_, transport->provider(toolingProfile(static_cast<std::size_t>(kClients))));
        RAWFRAME_TRY_ASSIGN(server_, ToolingServer::create(*provider_, std::move(settings)));
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (server_ == nullptr) {
            return {};
        }
        RAWFRAME_TRY(provider_->listen(network::Endpoint{endpoint_}));
        emitter_.log(diagnostics::Severity::Info,
                     kListening,
                     "the tooling endpoint is listening",
                     {diagnostics::field("endpoint", std::string_view{endpoint_})});
        return {};
    }

    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (server_ != nullptr) {
            server_->serve(simulation_->world(), simulation_->tick(), frame.now);
        }
    }

    void stop() noexcept override {
        if (server_ == nullptr) {
            return;
        }
        const ToolingServer::Statistics kStatistics = server_->statistics();
        emitter_.log(diagnostics::Severity::Info,
                     kSummary,
                     "tooling endpoint totals",
                     {diagnostics::field("accepted", kStatistics.accepted),
                      diagnostics::field("admitted", kStatistics.admitted),
                      diagnostics::field("refused", kStatistics.refused),
                      diagnostics::field("records", kStatistics.records)});
        server_.reset();
        provider_.reset();
    }

private:
    std::string endpoint_;
    world_runtime::Simulation* simulation_ = nullptr;
    std::unique_ptr<network::Provider> provider_;
    std::unique_ptr<ToolingServer> server_;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makeEndpoint(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<EndpointParticipant>();
    const auto kEndpoint = context.configuration().text("tooling.endpoint");
    if (kEndpoint.has_value()) {
        RAWFRAME_TRY(participant->load(context, std::string{*kEndpoint}));
    }
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.tooling.endpoint",
        .factory = &makeEndpoint,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "tooling.endpoint",
        .budgetOwner = "network",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Ingress),
    });
}

} // namespace rawframe::world_tooling
