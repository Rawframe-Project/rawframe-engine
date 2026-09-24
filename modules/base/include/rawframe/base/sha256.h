#pragma once

// SHA-256 (FIPS 180-4), for fingerprints: exact compatibility evidence,
// content identity, checkpoint integrity. Not for secrets or passwords.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::base {

using Sha256Digest = std::array<std::byte, 32>;

/// An incremental digest: feed bytes in any number of pieces, then finish.
class Sha256 {
public:
    Sha256() noexcept;

    void update(std::span<const std::byte> bytes) noexcept;
    void update(std::string_view text) noexcept {
        update(std::as_bytes(std::span{text.data(), text.size()}));
    }
    /// The digest of everything fed. The object is spent afterwards.
    [[nodiscard]] Sha256Digest finish() noexcept;

private:
    void block(const std::byte* data) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::byte, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] Sha256Digest sha256(std::string_view text) noexcept;

} // namespace rawframe::base
