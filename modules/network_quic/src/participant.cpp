#include "allocations.h"
#include "rawframe/composition/composition.h"
#include "rawframe/network/errors.h"
#include "rawframe/network/transport.h"
#include "rawframe/network_quic/errors.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/network_quic/registrar.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace rawframe::network_quic {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kReady{"network_quic", "ready"};
constexpr EventIdentity kUnwritten{"network_quic", "fingerprint_unwritten"};
constexpr EventIdentity kRenewed{"network_quic", "identity_renewed"};
constexpr EventIdentity kUnrenewed{"network_quic", "identity_unrenewed"};
/// A self-signed identity's days: thirty, or thirteen where browsers
/// connect, since a browser pins a certificate by its hash only if it lives
/// at most fourteen.
constexpr std::uint32_t kSelfSignedDays = 30;
constexpr std::uint32_t kBrowserSelfSignedDays = 13;
constexpr std::int64_t kMillisecondsPerDay = 86'400'000;
/// After a renewal that failed, the next try.
constexpr execution::MonotonicDuration kRenewAgain = execution::MonotonicDuration::fromSeconds(60);
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

/// Writes the file whole beside itself, then renames it into place, so a
/// reader waiting for it never reads it half written.
result::Status writeFile(const std::string& path, std::string_view text) {
    const std::string kPart = path + ".part";
    std::FILE* file = std::fopen(kPart.c_str(), "wb");
    if (file == nullptr) {
        return badFile("the fingerprint file cannot be written");
    }
    const bool kWritten = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (std::fclose(file) != 0 || !kWritten) {
        return badFile("the fingerprint file cannot be written");
    }
    std::error_code error;
    std::filesystem::rename(kPart, path, error);
    if (error) {
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

/// A self-signed identity's renewal (D419): how long after the first
/// iteration, and for how many days each new one lives.
struct Renewal {
    execution::MonotonicDuration after;
    std::uint32_t days = 0;
};

class QuicTransport final : public composition::Participant, public network::Transport {
public:
    QuicTransport(std::unique_ptr<QuicNetwork> network,
                  std::optional<Fingerprint> identity,
                  std::optional<std::string> fingerprintFile,
                  std::optional<Renewal> renewal)
        : network_(std::move(network)), identity_(identity), fingerprintFile_(std::move(fingerprintFile)),
          renewal_(renewal) {
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
        context_ = &context;
        context.reportMemory(quicHeapBytes().value_or(0));
        const std::string kFingerprint = identity_.has_value() ? formatFingerprint(*identity_) : std::string{};
        context.emitter().log(diagnostics::Severity::Info,
                              kReady,
                              "the QUIC transport is ready",
                              {diagnostics::field("fingerprint", std::string_view{kFingerprint})});
        return {};
    }

    /// MsQuic's heap, twice a second at 120 iterations (D234). On the first
    /// iteration, the fingerprint file: every participant has started, so
    /// whatever listens through this transport is listening, and a launcher
    /// that waits for the file can start its client (D395).
    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (context_ == nullptr) {
            return;
        }
        if (frame.iteration % 60 == 0) {
            context_->reportMemory(quicHeapBytes().value_or(0));
        }
        if (!written_) {
            written_ = true;
            writeFingerprint();
            if (renewal_.has_value()) {
                renewAt_ = frame.now + renewal_->after;
            }
        } else if (unwritten_ && frame.iteration % 60 == 0) {
            writeFingerprint();
        }
        if (renewAt_.has_value() && frame.now >= *renewAt_) {
            renew(frame.now);
        }
    }

private:
    /// The fingerprint file, whole or not at all. Unwritten, it is tried
    /// again twice a second and its error logged once: on Windows the
    /// rename fails while another process, a virus scanner among them,
    /// holds the old file open (D585).
    void writeFingerprint() {
        if (!fingerprintFile_.has_value() || !identity_.has_value()) {
            return;
        }
        const bool kWasUnwritten = unwritten_;
        unwritten_ = !writeFile(*fingerprintFile_, formatFingerprint(*identity_) + "\n").has_value();
        if (unwritten_ && !kWasUnwritten) {
            context_->emitter().log(diagnostics::Severity::Error,
                                    kUnwritten,
                                    "the fingerprint file cannot be written",
                                    {diagnostics::field("path", std::string_view{*fingerprintFile_})});
        }
    }

    /// A new self-signed identity, for connections from now on, and its
    /// fingerprint in the file, so a page or launcher reading it pins the
    /// identity the server now presents.
    void renew(execution::MonotonicInstant now) {
        auto made = makeSelfSignedCertificate("rawframe-server", renewal_->days);
        auto fingerprint =
            made.has_value() ? fingerprintOf(*made) : std::unexpected<result::Error>{made.error().clone()};
        const result::Status kDone = fingerprint.has_value()
                                         ? network_->renew(*made)
                                         : result::Status{std::unexpected{fingerprint.error().clone()}};
        if (!kDone.has_value()) {
            renewAt_ = now + kRenewAgain;
            context_->emitter().log(diagnostics::Severity::Error,
                                    kUnrenewed,
                                    "the server's identity could not be renewed; it is tried again in a minute",
                                    {diagnostics::field("reason", std::string{kDone.error().description()})});
            return;
        }
        identity_ = *fingerprint;
        renewAt_ = now + renewal_->after;
        writeFingerprint();
        const std::string kFingerprint = formatFingerprint(*identity_);
        context_->emitter().log(diagnostics::Severity::Info,
                                kRenewed,
                                "the server's identity is renewed: new connections are presented it",
                                {diagnostics::field("fingerprint", std::string_view{kFingerprint})});
    }

    composition::ParticipantContext* context_ = nullptr;
    std::unique_ptr<QuicNetwork> network_;
    std::optional<Fingerprint> identity_;
    /// Where the fingerprint goes on the first iteration and each renewal.
    std::optional<std::string> fingerprintFile_;
    bool written_ = false;
    /// The last write of the fingerprint file failed.
    bool unwritten_ = false;
    std::optional<Renewal> renewal_;
    std::optional<execution::MonotonicInstant> renewAt_;
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
        RAWFRAME_TRY_ASSIGN(
            Certificate made,
            makeSelfSignedCertificate("rawframe-server", browsers ? kBrowserSelfSignedDays : kSelfSignedDays));
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
    }
    // A self-signed identity is renewed a day before it would end, or
    // `network.quic.renew_ms` after the first iteration and each renewal.
    std::optional<Renewal> renewal;
    const auto kRenewText = configuration.text("network.quic.renew_ms");
    if (configuration.text("network.quic.self_signed") == "true") {
        const std::uint32_t kDays = settings.webTransport ? kBrowserSelfSignedDays : kSelfSignedDays;
        const std::int64_t kLongest = static_cast<std::int64_t>(kDays - 1) * kMillisecondsPerDay;
        RAWFRAME_TRY_ASSIGN(
            const std::uint64_t kRenewMs,
            configuration.unsignedInteger("network.quic.renew_ms", static_cast<std::uint64_t>(kLongest)));
        if (kRenewMs < 1000 || kRenewMs > static_cast<std::uint64_t>(kLongest)) {
            return badFile("network.quic.renew_ms is at least 1000 and ends a day before the identity does");
        }
        renewal = Renewal{.after = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kRenewMs)),
                          .days = kDays};
    } else if (kRenewText.has_value()) {
        return badFile("network.quic.renew_ms renews a self-signed identity alone");
    }
    std::optional<std::string> fingerprintFile = configuration.path("network.quic.fingerprint_file");
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<QuicNetwork> network, QuicNetwork::create(std::move(settings)));
    return composition::ParticipantOwner{
        new QuicTransport{std::move(network), identity, std::move(fingerprintFile), renewal}};
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
        .budgetOwner = "network",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Maintenance),
    });
}

} // namespace rawframe::network_quic
