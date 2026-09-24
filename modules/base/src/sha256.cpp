#include "rawframe/base/sha256.h"

#include <algorithm>
#include <cstring>

namespace rawframe::base {

namespace {

constexpr std::array<std::uint32_t, 64> kRounds = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotate(std::uint32_t value, unsigned by) noexcept {
    return (value >> by) | (value << (32U - by));
}

} // namespace

Sha256::Sha256() noexcept
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {
}

void Sha256::block(const std::byte* data) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = (std::to_integer<std::uint32_t>(data[index * 4]) << 24U) |
                       (std::to_integer<std::uint32_t>(data[(index * 4) + 1]) << 16U) |
                       (std::to_integer<std::uint32_t>(data[(index * 4) + 2]) << 8U) |
                       std::to_integer<std::uint32_t>(data[(index * 4) + 3]);
    }
    for (std::size_t index = 16; index < 64; ++index) {
        const std::uint32_t kLow =
            rotate(words[index - 15], 7) ^ rotate(words[index - 15], 18) ^ (words[index - 15] >> 3U);
        const std::uint32_t kHigh =
            rotate(words[index - 2], 17) ^ rotate(words[index - 2], 19) ^ (words[index - 2] >> 10U);
        words[index] = words[index - 16] + kLow + words[index - 7] + kHigh;
    }
    std::array<std::uint32_t, 8> working = state_;
    for (std::size_t index = 0; index < 64; ++index) {
        const std::uint32_t kSumHigh = rotate(working[4], 6) ^ rotate(working[4], 11) ^ rotate(working[4], 25);
        const std::uint32_t kChoose = (working[4] & working[5]) ^ (~working[4] & working[6]);
        const std::uint32_t kFirst = working[7] + kSumHigh + kChoose + kRounds[index] + words[index];
        const std::uint32_t kSumLow = rotate(working[0], 2) ^ rotate(working[0], 13) ^ rotate(working[0], 22);
        const std::uint32_t kMajority =
            (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
        const std::uint32_t kSecond = kSumLow + kMajority;
        working[7] = working[6];
        working[6] = working[5];
        working[5] = working[4];
        working[4] = working[3] + kFirst;
        working[3] = working[2];
        working[2] = working[1];
        working[1] = working[0];
        working[0] = kFirst + kSecond;
    }
    for (std::size_t index = 0; index < 8; ++index) {
        state_[index] += working[index];
    }
}

void Sha256::update(std::span<const std::byte> bytes) noexcept {
    length_ += bytes.size();
    std::size_t at = 0;
    if (buffered_ != 0) {
        const std::size_t kTake = std::min(bytes.size(), buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, bytes.data(), kTake);
        buffered_ += kTake;
        at = kTake;
        if (buffered_ < buffer_.size()) {
            return;
        }
        block(buffer_.data());
        buffered_ = 0;
    }
    for (; at + 64 <= bytes.size(); at += 64) {
        block(bytes.data() + at);
    }
    if (at < bytes.size()) {
        std::memcpy(buffer_.data(), bytes.data() + at, bytes.size() - at);
        buffered_ = bytes.size() - at;
    }
}

Sha256Digest Sha256::finish() noexcept {
    // A one bit, zeros to 56 bytes into a block, then the length in bits.
    const std::uint64_t kBits = length_ * 8U;
    const std::byte kOne{0x80};
    update(std::span{&kOne, 1});
    const std::byte kZero{0};
    while (buffered_ != 56) {
        update(std::span{&kZero, 1});
    }
    std::array<std::byte, 8> length{};
    for (std::size_t index = 0; index < 8; ++index) {
        length[index] = static_cast<std::byte>((kBits >> (8U * (7U - index))) & 0xFFU);
    }
    update(length);
    Sha256Digest digest{};
    for (std::size_t index = 0; index < 8; ++index) {
        for (std::size_t part = 0; part < 4; ++part) {
            digest[(index * 4) + part] = static_cast<std::byte>((state_[index] >> (8U * (3U - part))) & 0xFFU);
        }
    }
    return digest;
}

Sha256Digest sha256(std::span<const std::byte> bytes) noexcept {
    Sha256 hasher;
    hasher.update(bytes);
    return hasher.finish();
}

Sha256Digest sha256(std::string_view text) noexcept {
    Sha256 hasher;
    hasher.update(text);
    return hasher.finish();
}

} // namespace rawframe::base
