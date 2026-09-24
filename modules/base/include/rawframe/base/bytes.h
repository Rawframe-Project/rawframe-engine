#pragma once

#include <cstddef>
#include <span>

// The byte, size, and index vocabulary the codebase uses. ADR-0075 is explicit
// that base does not wrap or reinvent std::span, std::string_view, or standard
// containers; it fixes which standard vocabulary is used and how. The substance
// of this header is therefore the five rules below, and the declarations are two.
//
// 1. Raw memory is std::byte. char, unsigned char, signed char, and void* are
//    not byte vocabulary. char remains correct only for text.
// 2. A width that matters is spelled from <cstdint>: std::uint32_t,
//    std::int64_t, and siblings. int, long, unsigned, and short do not appear in
//    a declaration whose width is part of its meaning.
// 3. A size, a count, or an index into memory is std::size_t. A signed offset is
//    std::ptrdiff_t. Neither is spelled as a fixed-width type, because their
//    widths are platform properties.
// 4. A borrowed range is std::span, borrowed text is std::string_view, and both
//    are non-owning borrows under STD-0004: they never outlive what they view
//    and never appear in an owning position.
// 5. Base declares no container, no allocator, no string type, and no arithmetic
//    helper. SPEC-0007 forbids the allocation service, the containers framework,
//    and the math domain to base by name; a string type is forbidden by
//    ADR-0075's closed list.

namespace rawframe::base {

/// A borrowed, immutable view of bytes. Non-owning, per STD-0004.
using ByteSpan = std::span<const std::byte>;

/// A borrowed, mutable view of bytes. Non-owning, per STD-0004.
using MutableByteSpan = std::span<std::byte>;

} // namespace rawframe::base
