#pragma once

// Application admission payloads, generation 1 (SPEC-0010 admission): what a
// client proves in `protocol_hello`, what a server grants in
// `protocol_accept`, and why it says no in `protocol_reject`. Decoding is
// exact: every field present, every length within its bound, nothing after
// the last field.

#include "rawframe/network/wire.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rawframe::network {

/// Control stream frame types (SPEC-0010 critical frame registry).
enum class ControlFrame : std::uint64_t {
    ProtocolHello = 0,
    ProtocolAccept = 2,
    ProtocolReject = 4,
    MappingDeclare = 6,
    MappingAck = 8,
    MappingRetire = 10,
    MappingRetireAck = 12,
    StateAck = 14,
    BaselineOffer = 16,
    BaselineResult = 18,
    Heartbeat = 20,
    GracefulClose = 22,
};

/// Exact compatibility evidence: 32 bytes, compared whole, never parsed.
struct Fingerprint {
    std::array<std::byte, 32> bytes{};

    friend constexpr bool operator==(const Fingerprint&, const Fingerprint&) noexcept = default;
};

/// The application protocol's own fingerprint for generation 1. Chosen once;
/// any change to the wire is a new generation with a new fingerprint.
[[nodiscard]] Fingerprint protocolFingerprint() noexcept;

inline constexpr std::uint64_t kProtocolGeneration = 1;

/// Everything two peers must agree on exactly.
struct Compatibility {
    Fingerprint protocol;
    Fingerprint game;
    Fingerprint package;
    Fingerprint schema;
    Fingerprint profile;

    friend bool operator==(const Compatibility&, const Compatibility&) noexcept = default;
};

/// A fresh per-admission value, so one admission cannot be replayed as
/// another.
using Nonce = std::array<std::byte, 16>;

/// Bounds on what an admission payload may carry.
inline constexpr std::size_t kMaximumSessionBytes = 64;
inline constexpr std::size_t kMaximumTicketBytes = 1024;
inline constexpr std::size_t kMaximumRejectText = 256;

struct Hello {
    std::uint64_t generation = kProtocolGeneration;
    Compatibility compatibility;
    /// Features the client cannot do without, and all it can do.
    std::uint64_t requiredFeatures = 0;
    std::uint64_t availableFeatures = 0;
    std::vector<std::byte> requestedSession;
    /// Opaque, validated only by the composed ticket validator.
    std::vector<std::byte> ticket;
    Nonce nonce{};
    /// The largest datagram and frame payload the client will take.
    std::uint64_t maximumDatagram = 0;
    std::uint64_t maximumFrame = 0;
};

struct Accept {
    Compatibility compatibility;
    std::uint64_t features = 0;
    std::vector<std::byte> session;
    /// The server's name for this connection, and its generation.
    std::uint64_t connection = 0;
    std::uint64_t connectionEpoch = 0;
    /// Nonzero epochs the lanes start in.
    std::uint64_t inputEpoch = 0;
    std::uint64_t replicationEpoch = 0;
    /// The authoritative tick rate as ticks per seconds, and the tick the
    /// connection was admitted at.
    std::uint64_t tickRateTicks = 0;
    std::uint64_t tickRateSeconds = 0;
    std::uint64_t tickOrigin = 0;
    /// The limits both sides now keep.
    std::uint64_t maximumDatagram = 0;
    std::uint64_t maximumFrame = 0;
    Nonce nonce{};
};

enum class RejectReason : std::uint64_t {
    ProtocolMismatch = 1,
    GameMismatch = 2,
    PackageMismatch = 3,
    SchemaMismatch = 4,
    ProfileMismatch = 5,
    FeatureMissing = 6,
    TicketInvalid = 7,
    Capacity = 8,
    Malformed = 9,
};

inline constexpr std::uint64_t kLastRejectReason = static_cast<std::uint64_t>(RejectReason::Malformed);

struct Reject {
    RejectReason reason = RejectReason::Malformed;
    /// Redacted and bounded; never a secret, a ticket, or a path.
    std::string message;
};

[[nodiscard]] result::Status encodeHello(Writer& writer, const Hello& hello);
[[nodiscard]] result::Result<Hello> decodeHello(std::span<const std::byte> payload);
[[nodiscard]] result::Status encodeAccept(Writer& writer, const Accept& accept);
[[nodiscard]] result::Result<Accept> decodeAccept(std::span<const std::byte> payload);
[[nodiscard]] result::Status encodeReject(Writer& writer, const Reject& reject);
[[nodiscard]] result::Result<Reject> decodeReject(std::span<const std::byte> payload);

/// The first difference between what a client offers and what a server
/// expects, as the reason the server gives; none when they agree.
[[nodiscard]] std::optional<RejectReason>
compare(const Hello& hello, const Compatibility& expected, std::uint64_t serverFeatures) noexcept;

} // namespace rawframe::network
