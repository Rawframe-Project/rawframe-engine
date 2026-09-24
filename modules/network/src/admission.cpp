#include "rawframe/network/admission.h"

#include "rawframe/network/errors.h"

namespace rawframe::network {

namespace {

std::unexpected<result::Error> malformed(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kNetworkDomain, code(NetworkError::Malformed), why);
}

result::Status writeFingerprint(Writer& writer, const Fingerprint& fingerprint) {
    return writer.bytes(fingerprint.bytes);
}

result::Status readFixed(Reader& reader, std::span<std::byte> into) {
    RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kBytes, reader.bytes(into.size()));
    std::copy(kBytes.begin(), kBytes.end(), into.begin());
    return {};
}

result::Status writeCompatibility(Writer& writer, const Compatibility& compatibility) {
    for (const Fingerprint* fingerprint : {&compatibility.protocol,
                                           &compatibility.game,
                                           &compatibility.package,
                                           &compatibility.schema,
                                           &compatibility.profile}) {
        RAWFRAME_TRY(writeFingerprint(writer, *fingerprint));
    }
    return {};
}

result::Result<Compatibility> readCompatibility(Reader& reader) {
    Compatibility compatibility;
    for (Fingerprint* fingerprint : {&compatibility.protocol,
                                     &compatibility.game,
                                     &compatibility.package,
                                     &compatibility.schema,
                                     &compatibility.profile}) {
        RAWFRAME_TRY(readFixed(reader, fingerprint->bytes));
    }
    return compatibility;
}

/// A length-prefixed run of at most `maximum` bytes.
result::Status writeBounded(Writer& writer, std::span<const std::byte> bytes) {
    RAWFRAME_TRY(writer.varint(bytes.size()));
    return writer.bytes(bytes);
}

result::Result<std::vector<std::byte>> readBounded(Reader& reader, std::size_t maximum) {
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLength, reader.varintAtMost(maximum));
    RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kBytes, reader.bytes(kLength));
    return std::vector<std::byte>{kBytes.begin(), kBytes.end()};
}

result::Status finished(const Reader& reader) {
    if (reader.remaining() != 0) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kNetworkDomain,
                            code(NetworkError::TrailingBytes),
                            "an admission payload continues past its last field");
    }
    return {};
}

} // namespace

Fingerprint protocolFingerprint() noexcept {
    constexpr std::array<unsigned char, 32> kBytes = {0x57, 0x82, 0x20, 0x2e, 0xbb, 0x99, 0x35, 0x8a, 0x79, 0x7e, 0x01,
                                                      0x64, 0xce, 0xf9, 0xa7, 0x8f, 0x7a, 0x74, 0x73, 0xfb, 0x27, 0xdb,
                                                      0x06, 0xa3, 0xed, 0x82, 0xbf, 0x48, 0x25, 0x02, 0xeb, 0x21};
    Fingerprint fingerprint;
    for (std::size_t index = 0; index < kBytes.size(); ++index) {
        fingerprint.bytes[index] = static_cast<std::byte>(kBytes[index]);
    }
    return fingerprint;
}

result::Status encodeHello(Writer& writer, const Hello& hello) {
    if (hello.requestedSession.size() > kMaximumSessionBytes || hello.ticket.size() > kMaximumTicketBytes) {
        return malformed("a hello's session or ticket is over its bound");
    }
    RAWFRAME_TRY(writer.varint(hello.generation));
    RAWFRAME_TRY(writeCompatibility(writer, hello.compatibility));
    RAWFRAME_TRY(writer.varint(hello.requiredFeatures));
    RAWFRAME_TRY(writer.varint(hello.availableFeatures));
    RAWFRAME_TRY(writeBounded(writer, hello.requestedSession));
    RAWFRAME_TRY(writeBounded(writer, hello.ticket));
    RAWFRAME_TRY(writer.bytes(hello.nonce));
    RAWFRAME_TRY(writer.varint(hello.maximumDatagram));
    return writer.varint(hello.maximumFrame);
}

result::Result<Hello> decodeHello(std::span<const std::byte> payload) {
    Reader reader{payload};
    Hello hello;
    RAWFRAME_TRY_ASSIGN(hello.generation, reader.varint());
    RAWFRAME_TRY_ASSIGN(hello.compatibility, readCompatibility(reader));
    RAWFRAME_TRY_ASSIGN(hello.requiredFeatures, reader.varint());
    RAWFRAME_TRY_ASSIGN(hello.availableFeatures, reader.varint());
    RAWFRAME_TRY_ASSIGN(hello.requestedSession, readBounded(reader, kMaximumSessionBytes));
    RAWFRAME_TRY_ASSIGN(hello.ticket, readBounded(reader, kMaximumTicketBytes));
    RAWFRAME_TRY(readFixed(reader, hello.nonce));
    RAWFRAME_TRY_ASSIGN(hello.maximumDatagram, reader.varint());
    RAWFRAME_TRY_ASSIGN(hello.maximumFrame, reader.varint());
    RAWFRAME_TRY(finished(reader));
    return hello;
}

result::Status encodeAccept(Writer& writer, const Accept& accept) {
    if (accept.session.size() > kMaximumSessionBytes) {
        return malformed("an accept's session is over its bound");
    }
    RAWFRAME_TRY(writeCompatibility(writer, accept.compatibility));
    RAWFRAME_TRY(writer.varint(accept.features));
    RAWFRAME_TRY(writeBounded(writer, accept.session));
    for (const std::uint64_t kValue : {accept.connection,
                                       accept.connectionEpoch,
                                       accept.inputEpoch,
                                       accept.replicationEpoch,
                                       accept.tickRateTicks,
                                       accept.tickRateSeconds,
                                       accept.tickOrigin,
                                       accept.maximumDatagram,
                                       accept.maximumFrame}) {
        RAWFRAME_TRY(writer.varint(kValue));
    }
    return writer.bytes(accept.nonce);
}

result::Result<Accept> decodeAccept(std::span<const std::byte> payload) {
    Reader reader{payload};
    Accept accept;
    RAWFRAME_TRY_ASSIGN(accept.compatibility, readCompatibility(reader));
    RAWFRAME_TRY_ASSIGN(accept.features, reader.varint());
    RAWFRAME_TRY_ASSIGN(accept.session, readBounded(reader, kMaximumSessionBytes));
    for (std::uint64_t* value : {&accept.connection,
                                 &accept.connectionEpoch,
                                 &accept.inputEpoch,
                                 &accept.replicationEpoch,
                                 &accept.tickRateTicks,
                                 &accept.tickRateSeconds,
                                 &accept.tickOrigin,
                                 &accept.maximumDatagram,
                                 &accept.maximumFrame}) {
        RAWFRAME_TRY_ASSIGN(*value, reader.varint());
    }
    RAWFRAME_TRY(readFixed(reader, accept.nonce));
    RAWFRAME_TRY(finished(reader));
    if (accept.inputEpoch == 0 || accept.replicationEpoch == 0 || accept.connectionEpoch == 0 ||
        accept.tickRateTicks == 0 || accept.tickRateSeconds == 0) {
        return malformed("an accept's epochs and tick rate are nonzero");
    }
    return accept;
}

result::Status encodeReject(Writer& writer, const Reject& reject) {
    if (reject.message.size() > kMaximumRejectText) {
        return malformed("a reject's message is over its bound");
    }
    RAWFRAME_TRY(writer.varint(static_cast<std::uint64_t>(reject.reason)));
    RAWFRAME_TRY(writer.varint(reject.message.size()));
    return writer.bytes(std::as_bytes(std::span{reject.message}));
}

result::Result<Reject> decodeReject(std::span<const std::byte> payload) {
    Reader reader{payload};
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kReason, reader.varint());
    if (kReason == 0 || kReason > kLastRejectReason) {
        return malformed("a reject gives no reason this generation knows");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kText, readBounded(reader, kMaximumRejectText));
    RAWFRAME_TRY(finished(reader));
    Reject reject{.reason = static_cast<RejectReason>(kReason), .message = {}};
    for (const std::byte kByte : kText) {
        reject.message.push_back(static_cast<char>(kByte));
    }
    return reject;
}

std::optional<RejectReason>
compare(const Hello& hello, const Compatibility& expected, std::uint64_t serverFeatures) noexcept {
    if (hello.generation != kProtocolGeneration || hello.compatibility.protocol != expected.protocol) {
        return RejectReason::ProtocolMismatch;
    }
    if (hello.compatibility.game != expected.game) {
        return RejectReason::GameMismatch;
    }
    if (hello.compatibility.package != expected.package) {
        return RejectReason::PackageMismatch;
    }
    if (hello.compatibility.schema != expected.schema) {
        return RejectReason::SchemaMismatch;
    }
    if (hello.compatibility.profile != expected.profile) {
        return RejectReason::ProfileMismatch;
    }
    if ((hello.requiredFeatures & ~serverFeatures) != 0) {
        return RejectReason::FeatureMissing;
    }
    return std::nullopt;
}

} // namespace rawframe::network
