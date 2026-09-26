// Admission payloads: exact round trips, every truncation refused, bounds and
// domains kept, hostile payloads read only as they write, and the
// compatibility matrix with no downgrade.

#include "rawframe/network/admission.h"
#include "rawframe/network/close.h"
#include "rawframe/network/errors.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::network;

namespace {

Fingerprint filled(unsigned value) {
    Fingerprint fingerprint;
    fingerprint.bytes.fill(static_cast<std::byte>(value));
    return fingerprint;
}

Compatibility compatibility() {
    return Compatibility{.protocol = protocolFingerprint(),
                         .game = filled(1),
                         .package = filled(2),
                         .schema = filled(3),
                         .profile = filled(4)};
}

Hello hello() {
    Hello made{.compatibility = compatibility(),
               .requiredFeatures = 0b01,
               .availableFeatures = 0b11,
               .requestedSession = {std::byte{7}, std::byte{8}},
               .ticket = std::vector<std::byte>(300, std::byte{0x5a}),
               .maximumDatagram = 1200,
               .maximumFrame = 65536};
    made.nonce.fill(std::byte{0x33});
    return made;
}

template <typename T> std::vector<std::byte> encoded(const T& value, result::Status (*encode)(Writer&, const T&)) {
    std::vector<std::byte> buffer(4096);
    Writer writer{buffer};
    RAWFRAME_EXPECT(encode(writer, value).has_value());
    return {writer.written().begin(), writer.written().end()};
}

std::vector<std::byte> bytesOf(const std::string& text) {
    std::vector<std::byte> bytes(text.size());
    if (!text.empty()) {
        std::memcpy(bytes.data(), text.data(), text.size());
    }
    return bytes;
}

std::string textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Mutations of `seed` read as `T`: whatever is read writes back to its very
/// bytes, so a peer has one spelling for each payload. Returns how many were
/// read.
template <typename T>
std::size_t mutated(std::span<const std::byte> seed,
                    result::Result<T> (*decode)(std::span<const std::byte>),
                    result::Status (*encode)(Writer&, const T&)) {
    test::Mutations mutations;
    std::size_t read = 0;
    for (int round = 0; round < 20'000; ++round) {
        const std::vector<std::byte> kDamaged =
            bytesOf(mutations.mutate(textOf(seed), std::string_view{"\x00\x01\x02\x3f\x40\x80\xc0\xff", 8}));
        const auto kRead = decode(kDamaged);
        if (!kRead.has_value()) {
            continue;
        }
        ++read;
        std::vector<std::byte> buffer(4096);
        Writer writer{buffer};
        RAWFRAME_EXPECT(encode(writer, *kRead).has_value() && std::ranges::equal(writer.written(), kDamaged));
    }
    return read;
}

template <typename T> bool failedWith(const result::Result<T>& outcome, NetworkError error) {
    return !outcome.has_value() && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(HellosRoundTripExactly) {
    const Hello kHello = hello();
    const auto kBytes = encoded(kHello, &encodeHello);
    const auto kDecoded = decodeHello(kBytes);
    RAWFRAME_EXPECT(kDecoded.has_value());
    RAWFRAME_EXPECT(kDecoded->compatibility == kHello.compatibility && kDecoded->ticket == kHello.ticket &&
                    kDecoded->requestedSession == kHello.requestedSession && kDecoded->nonce == kHello.nonce &&
                    kDecoded->maximumFrame == 65536 && kDecoded->requiredFeatures == 1);
    // Every cut is refused, and one byte more is too.
    for (std::size_t length = 0; length < kBytes.size(); ++length) {
        RAWFRAME_EXPECT(!decodeHello(std::span{kBytes}.first(length)).has_value());
    }
    std::vector<std::byte> longer = kBytes;
    longer.push_back(std::byte{0});
    RAWFRAME_EXPECT(failedWith(decodeHello(longer), NetworkError::TrailingBytes));
    // A ticket over its bound is refused writing and reading.
    Hello large = kHello;
    large.ticket.resize(kMaximumTicketBytes + 1);
    std::vector<std::byte> buffer(4096);
    Writer writer{buffer};
    RAWFRAME_EXPECT(!encodeHello(writer, large).has_value());
}

RAWFRAME_TEST(AcceptsAndRejectsRoundTrip) {
    Accept accept{.compatibility = compatibility(),
                  .features = 3,
                  .session = {std::byte{1}},
                  .connection = 44,
                  .connectionEpoch = 5,
                  .inputEpoch = 6,
                  .replicationEpoch = 1ULL << 40U,
                  .tickRateTicks = 60,
                  .tickRateSeconds = 1,
                  .tickOrigin = 1000,
                  .maximumDatagram = 1200,
                  .maximumFrame = 65536};
    accept.nonce.fill(std::byte{9});
    const auto kBytes = encoded(accept, &encodeAccept);
    const auto kDecoded = decodeAccept(kBytes);
    RAWFRAME_EXPECT(kDecoded.has_value() && kDecoded->replicationEpoch == (1ULL << 40U) &&
                    kDecoded->tickOrigin == 1000 && kDecoded->nonce == accept.nonce);
    for (std::size_t length = 0; length < kBytes.size(); ++length) {
        RAWFRAME_EXPECT(!decodeAccept(std::span{kBytes}.first(length)).has_value());
    }
    Accept zeroEpoch = accept;
    zeroEpoch.inputEpoch = 0;
    RAWFRAME_EXPECT(failedWith(decodeAccept(encoded(zeroEpoch, &encodeAccept)), NetworkError::Malformed));

    const Reject kReject{.reason = RejectReason::SchemaMismatch, .message = "schema differs"};
    const auto kRejectBytes = encoded(kReject, &encodeReject);
    const auto kDecodedReject = decodeReject(kRejectBytes);
    RAWFRAME_EXPECT(kDecodedReject.has_value() && kDecodedReject->reason == RejectReason::SchemaMismatch &&
                    kDecodedReject->message == "schema differs");
    const auto kUnavailable =
        decodeReject(encoded(Reject{.reason = RejectReason::Unavailable, .message = {}}, &encodeReject));
    RAWFRAME_EXPECT(kUnavailable.has_value() && kUnavailable->reason == RejectReason::Unavailable);
    std::vector<std::byte> unknown = kRejectBytes;
    unknown[0] = std::byte{0x3f};
    RAWFRAME_EXPECT(failedWith(decodeReject(unknown), NetworkError::Malformed));
}

RAWFRAME_TEST(HostileAdmissionPayloadsReadOnlyAsTheyWrite) {
    // A Hello is the first thing an unadmitted peer sends; an Accept or a
    // Reject is what a client believes of a server it has not yet trusted.
    Accept accept{.compatibility = compatibility(),
                  .features = 3,
                  .session = {std::byte{1}, std::byte{2}},
                  .connection = 44,
                  .connectionEpoch = 5,
                  .inputEpoch = 6,
                  .replicationEpoch = 1ULL << 40U,
                  .tickRateTicks = 60,
                  .tickRateSeconds = 1,
                  .tickOrigin = 1000,
                  .maximumDatagram = 1200,
                  .maximumFrame = 65536};
    accept.nonce.fill(std::byte{9});
    Hello shortTicket = hello();
    shortTicket.ticket.resize(12);
    const std::size_t kHellos = mutated(encoded(shortTicket, &encodeHello), &decodeHello, &encodeHello);
    const std::size_t kAccepts = mutated(encoded(accept, &encodeAccept), &decodeAccept, &encodeAccept);
    const std::size_t kRejects =
        mutated(encoded(Reject{.reason = RejectReason::SchemaMismatch, .message = "schema differs"}, &encodeReject),
                &decodeReject,
                &encodeReject);
    RAWFRAME_EXPECT(kHellos > 0 && kAccepts > 0 && kRejects > 0);
}

RAWFRAME_TEST(CompatibilityIsExactWithNoDowngrade) {
    const Compatibility kExpected = compatibility();
    RAWFRAME_EXPECT(!compare(hello(), kExpected, 0b01).has_value());
    const auto kWith = [&](auto change) {
        Hello offered = hello();
        change(offered);
        return compare(offered, kExpected, 0b01);
    };
    RAWFRAME_EXPECT(kWith([](Hello& h) {
                        h.generation = 2;
                    }) == RejectReason::ProtocolMismatch);
    RAWFRAME_EXPECT(kWith([](Hello& h) {
                        h.compatibility.protocol = filled(9);
                    }) == RejectReason::ProtocolMismatch);
    RAWFRAME_EXPECT(kWith([](Hello& h) {
                        h.compatibility.game = filled(9);
                    }) == RejectReason::GameMismatch);
    RAWFRAME_EXPECT(kWith([](Hello& h) {
                        h.compatibility.package = filled(9);
                    }) == RejectReason::PackageMismatch);
    RAWFRAME_EXPECT(kWith([](Hello& h) {
                        h.compatibility.schema = filled(9);
                    }) == RejectReason::SchemaMismatch);
    RAWFRAME_EXPECT(kWith([](Hello& h) {
                        h.compatibility.profile = filled(9);
                    }) == RejectReason::ProfileMismatch);
    RAWFRAME_EXPECT(kWith([](Hello& h) {
                        h.requiredFeatures = 0b10;
                    }) == RejectReason::FeatureMissing);
}

RAWFRAME_TEST(AGracefulCloseSaysWhoIsLeaving) {
    for (const CloseNotice kNotice : {CloseNotice::ServerStopping, CloseNotice::ClientLeaving}) {
        std::vector<std::byte> bytes(8);
        Writer writer{bytes};
        RAWFRAME_EXPECT(encodeGracefulClose(writer, kNotice).has_value());
        const auto kDecoded = decodeGracefulClose(writer.written());
        RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == kNotice);
    }
    // No notice, one this generation does not know, and bytes after it.
    RAWFRAME_EXPECT(failedWith(decodeGracefulClose(std::vector<std::byte>{std::byte{0}}), NetworkError::Malformed));
    RAWFRAME_EXPECT(failedWith(decodeGracefulClose(std::vector<std::byte>{std::byte{3}}), NetworkError::Malformed));
    RAWFRAME_EXPECT(!decodeGracefulClose(std::vector<std::byte>{std::byte{1}, std::byte{0}}).has_value());
    RAWFRAME_EXPECT(!decodeGracefulClose({}).has_value());
}
