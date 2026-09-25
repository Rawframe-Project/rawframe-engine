#pragma once

// The part of QPACK (RFC 9204) a WebTransport server needs (D172): decoding
// a request's field section that refers to nothing dynamic, since this side
// announces no dynamic table, with literals plain or Huffman-coded (RFC 7541
// Appendix B); and encoding a response's status.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rawframe::network_quic {

struct FieldLine {
    std::string name;
    std::string value;
};

/// The field lines of a field section, or nothing for one that is
/// malformed, refers to a dynamic table, or decodes to more than
/// `maximumBytes` of names and values.
[[nodiscard]] std::optional<std::vector<FieldLine>> decodeFieldSection(std::span<const std::byte> section,
                                                                       std::size_t maximumBytes);

/// A field section of one `:status` line: indexed where the static table
/// holds it (200), else a literal with the static table's name.
[[nodiscard]] std::vector<std::byte> encodeStatus(std::uint16_t status);

/// A Huffman-coded string decoded, or nothing for bad padding, EOS inside
/// it, or more than `maximumBytes`. Exposed for its tests.
[[nodiscard]] std::optional<std::string> decodeHuffman(std::span<const std::byte> coded, std::size_t maximumBytes);

} // namespace rawframe::network_quic
