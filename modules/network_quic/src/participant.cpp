#include "rawframe/composition/composition.h"
#include "rawframe/network/errors.h"
#include "rawframe/network/transport.h"
#include "rawframe/network_quic/errors.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/network_quic/registrar.h"

#include <cstdio>
#include <string>

namespace rawframe::network_quic {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kReady{"network_quic", "ready"};
constexpr std::string_view kProvided[] = {network::kTransport.name};
/// A PEM file or a fingerprint file larger than this is not one.
constexpr std::size_t kMaximumFileBytes = 64 * 1024;

std::unexpected<result::Error> badFile(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kQuicDomain, code(QuicError::BadCertificate), why);
}

result::Result<std::string> readFile(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return badFile("a QUIC identity or pin file cannot be opened");
    }
    std::string text(kMaximumFileBytes + 1, '\0');
    const std::size_t kRead = std::fread(text.data(), 1, text.size(), file);
    const bool kFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (kFailed || kRead > kMaximumFileBytes) {
        return badFile("a QUIC identity or pin file cannot be read, or is too large");
    }
    text.resize(kRead);
    return text;
}

result::Status writeFile(const std::string& path, std::string_view text) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return badFile("the fingerprint file cannot be written");
    }
    const bool kWritten = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (std::fclose(file) != 0 || !kWritten) {
        return badFile("the fingerprint file cannot be written");
    }
    return {};
}

std::string_view trimmed(std::string_view text) noexcept {
    constexpr std::string_view kSpace = " \t\r\n";
    const std::size_t kFirst = text.find_first_not_of(kSpace);
    if (kFirst == std::string_view::npos) {
        return {};
    }
    return text.substr(kFirst, text.find_last_not_of(kSpace) - kFirst + 1);
}

class QuicTransport final : public composition::Participant, public network::Transport {
public:
    explicit QuicTransport(std::unique_ptr<QuicNetwork> network, std::optional<Fingerprint> identity)
        : network_(std::move(network)), identity_(identity) {
    }

    result::Result<std::unique_ptr<network::Provider>> provider(const network::ProviderProfile& profile) override {
        return network_->provider(profile);
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == network::kTransport.name) {
            return composition::provideAs<network::Transport>(*this);
        }
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        const std::string kFingerprint = identity_.has_value() ? formatFingerprint(*identity_) : std::string{};
        context.emitter().log(diagnostics::Severity::Info,
                              kReady,
                              "the QUIC transport is ready",
                              {diagnostics::field("fingerprint", std::string_view{kFingerprint})});
        return {};
    }

private:
    std::unique_ptr<QuicNetwork> network_;
    std::optional<Fingerprint> identity_;
};

result::Result<std::optional<Certificate>> identityOf(const composition::Configuration& configuration, bool browsers) {
    const auto kCertificateFile = configuration.path("network.quic.certificate_file");
    const auto kKeyFile = configuration.path("network.quic.private_key_file");
    const auto kSelfSigned = configuration.text("network.quic.self_signed");
    if (kSelfSigned.has_value() && *kSelfSigned != "true" && *kSelfSigned != "false") {
        return badFile("network.quic.self_signed is true or false");
    }
    const bool kMake = kSelfSigned == "true";
    if (kCertificateFile.has_value() != kKeyFile.has_value() || (kMake && kCertificateFile.has_value())) {
        return badFile("a QUIC identity is a certificate and key file pair, or self-signed, not both");
    }
    if (kMake) {
        // Thirty days: an identity made at start lives as long as the
        // process. Thirteen where browsers connect: a browser pins a
        // certificate by its hash only if it lives at most fourteen.
        RAWFRAME_TRY_ASSIGN(Certificate made, makeSelfSignedCertificate("rawframe-server", browsers ? 13 : 30));
        return std::optional<Certificate>{std::move(made)};
    }
    if (!kCertificateFile.has_value()) {
        return std::optional<Certificate>{};
    }
    Certificate read;
    RAWFRAME_TRY_ASSIGN(read.certificatePem, readFile(std::string{*kCertificateFile}));
    RAWFRAME_TRY_ASSIGN(read.privateKeyPem, readFile(std::string{*kKeyFile}));
    return std::optional<Certificate>{std::move(read)};
}

result::Result<std::optional<Fingerprint>> pinOf(const composition::Configuration& configuration) {
    const auto kPin = configuration.text("network.quic.pin");
    const auto kPinFile = configuration.path("network.quic.pin_file");
    if (kPin.has_value() && kPinFile.has_value()) {
        return badFile("network.quic.pin and network.quic.pin_file are one or the other");
    }
    if (kPin.has_value()) {
        RAWFRAME_TRY_ASSIGN(const Fingerprint kParsed, parseFingerprint(trimmed(*kPin)));
        return std::optional<Fingerprint>{kParsed};
    }
    if (kPinFile.has_value()) {
        RAWFRAME_TRY_ASSIGN(const std::string kText, readFile(std::string{*kPinFile}));
        RAWFRAME_TRY_ASSIGN(const Fingerprint kParsed, parseFingerprint(trimmed(kText)));
        return std::optional<Fingerprint>{kParsed};
    }
    return std::optional<Fingerprint>{};
}

result::Result<composition::ParticipantOwner> makeQuic(composition::ParticipantContext& context) noexcept {
    const composition::Configuration& configuration = context.configuration();
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kIdle,
                        configuration.unsignedInteger("network.quic.idle_timeout_ms", 10'000));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kKeepAlive,
                        configuration.unsignedInteger("network.quic.keep_alive_ms", 2'000));
    if (kIdle > 3'600'000 || kKeepAlive > 3'600'000) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            network::kNetworkDomain,
                            network::code(network::NetworkError::InvalidProfile),
                            "a QUIC timeout is at most an hour");
    }
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kProcessors, configuration.unsignedInteger("network.quic.processors", 0));
    if (kProcessors > 1024) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            network::kNetworkDomain,
                            network::code(network::NetworkError::InvalidProfile),
                            "network.quic.processors is at most 1024");
    }
    const auto kBrowsers = configuration.text("network.quic.webtransport");
    if (kBrowsers.has_value() && *kBrowsers != "true" && *kBrowsers != "false") {
        return badFile("network.quic.webtransport is true or false");
    }
    QuicSettings settings{
        .idleTimeout = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kIdle)),
        .keepAlive = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kKeepAlive)),
        .webTransport = kBrowsers == "true",
        .processors = static_cast<std::uint32_t>(kProcessors)};
    RAWFRAME_TRY_ASSIGN(settings.certificate, identityOf(configuration, settings.webTransport));
    RAWFRAME_TRY_ASSIGN(settings.pin, pinOf(configuration));
    std::optional<Fingerprint> identity;
    if (settings.certificate.has_value()) {
        RAWFRAME_TRY_ASSIGN(identity, fingerprintOf(*settings.certificate));
        if (const auto kPath = configuration.path("network.quic.fingerprint_file")) {
            RAWFRAME_TRY(writeFile(std::string{*kPath}, formatFingerprint(*identity) + "\n"));
        }
    }
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<QuicNetwork> network, QuicNetwork::create(std::move(settings)));
    return composition::ParticipantOwner{new QuicTransport{std::move(network), identity}};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.network.quic",
        .factory = &makeQuic,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvided,
        // Closing connections waits for their peers' acknowledgement.
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromSeconds(2)},
        .observabilityIdentity = "network.quic",
    });
}

} // namespace rawframe::network_quic
