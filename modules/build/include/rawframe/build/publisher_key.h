#pragma once

// A publisher's signing key, as packaging tooling holds it (SPEC-0021,
// SPEC-0023): an Ed25519 seed under a key identity, kept in a file of its
// own that never leaves the publisher, and the publisher key set that lists
// it. With the platform deferred (ADR-0084) the key set is made here and
// pinned by whoever verifies, as a standalone export pins its key material.

#include "rawframe/result/result.h"
#include "rawframe/signature/signature.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace rawframe::build {

struct PublisherKey {
    std::string publisher;
    std::string kid;
    std::array<std::byte, 32> seed{};
};

/// A fresh key for `publisher` (an ADR-0023 publisher segment), its seed
/// and key identity from the operating system's secure source.
[[nodiscard]] result::Result<PublisherKey> generatePublisherKey(std::string_view publisher);

/// The key's file: the canonical record `{publisher, kid, seed}`, the seed
/// as 64 lowercase hexadecimal characters. A secret.
[[nodiscard]] std::string writePublisherKey(const PublisherKey& key);
[[nodiscard]] result::Result<PublisherKey> readPublisherKey(std::string_view text);

/// The key's public half.
[[nodiscard]] result::Result<signature::PublicKey> publicKeyOf(const PublisherKey& key);

/// A publisher key set of this one key, active since `now`, sequence 1.
/// Until a key-event log exists, its head is the digest of the canonical
/// record `{kid, public_key}` of the key it was made with.
[[nodiscard]] result::Result<signature::PublisherKeySet> keySetOf(const PublisherKey& key, std::int64_t now);

/// The key's detached signature of exactly `message`.
[[nodiscard]] result::Result<signature::Envelope> sign(const PublisherKey& key, std::span<const std::byte> message);

} // namespace rawframe::build
