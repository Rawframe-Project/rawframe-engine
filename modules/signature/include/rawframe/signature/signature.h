#pragma once

// Ed25519 signatures as Rawframe carries them: one algorithm and no
// algorithm field anywhere (SPEC-0019), a detached `{kid, sig}` envelope
// over exact record bytes, and publisher key sets (SPEC-0023) whose typed
// key states decide a verdict fail-closed.

#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::signature {

using PublicKey = std::array<std::byte, 32>;
using SignatureBytes = std::array<std::byte, 64>;

/// Whether `signature` is `key`'s Ed25519 signature (RFC 8032) of exactly
/// `message`.
[[nodiscard]] bool
verify(const PublicKey& key, std::span<const std::byte> message, const SignatureBytes& signature) noexcept;

/// Whether `text` is a key identity: exactly 16 lowercase hexadecimal
/// characters.
[[nodiscard]] bool validKeyId(std::string_view text) noexcept;

/// A detached signature: which key, and its signature, 128 lowercase
/// hexadecimal characters in the record.
struct Envelope {
    std::string kid;
    SignatureBytes sig{};
};

/// The envelope's canonical record, `{kid, sig}`, or why not.
[[nodiscard]] result::Result<Envelope> readEnvelope(std::string_view text);
[[nodiscard]] std::string writeEnvelope(const Envelope& envelope);

enum class KeyState : std::uint8_t {
    Active,
    Retired,
    Revoked
};

struct PublisherKey {
    std::string kid;
    PublicKey publicKey{};
    KeyState state = KeyState::Active;
    /// The Unix second it entered its state.
    std::int64_t since = 0;
};

/// SPEC-0023's publisher key set: one publisher's keys, retired and revoked
/// ones listed forever.
struct PublisherKeySet {
    std::string publisher;
    std::int64_t sequence = 0;
    std::int64_t updatedAt = 0;
    std::string head;
    std::vector<PublisherKey> keys;
};

/// The key set's canonical record, closed schema, at most 16 KiB, 1 to 64
/// keys of which at most 16 active, no key identity twice; refused
/// (`Malformed`) otherwise.
[[nodiscard]] result::Result<PublisherKeySet> readPublisherKeySet(std::string_view text);
[[nodiscard]] result::Result<std::string> writePublisherKeySet(const PublisherKeySet& keys);

/// SPEC-0023's verdict on `envelope` over `message`: an active or retired
/// key's valid signature passes; a revoked key is refused (`KeyRevoked`)
/// whatever the signature; a key the set does not list is refused
/// (`UnknownKey`), after which the caller may refresh its set once and ask
/// again; and a signature that does not verify is refused (`BadSignature`).
[[nodiscard]] result::Status
verifyPublished(const PublisherKeySet& keys, std::span<const std::byte> message, const Envelope& envelope);

} // namespace rawframe::signature
