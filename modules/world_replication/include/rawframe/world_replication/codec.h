#pragma once

// How a component's value crosses the wire (SPEC-0010, SPEC-0006): field by
// field, each at its declared width in network byte order, never as a native
// struct dump. A component's codec is fixed for the admitted replication
// table, so a value's wire size is known before it is read.

#include "rawframe/network/wire.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/stable_id.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::world_replication {

/// What one field of a replicated value is. Floats cross as their IEEE-754
/// bits; a truth as one byte, nought or one.
enum class WireKind : std::uint8_t {
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    F32,
    F64,
    Bool
};

[[nodiscard]] constexpr std::size_t widthOf(WireKind kind) noexcept {
    switch (kind) {
    case WireKind::I8:
    case WireKind::U8:
    case WireKind::Bool:
        return 1;
    case WireKind::I16:
    case WireKind::U16:
        return 2;
    case WireKind::I32:
    case WireKind::U32:
    case WireKind::F32:
        return 4;
    case WireKind::I64:
    case WireKind::U64:
    case WireKind::F64:
        return 8;
    }
    return 0;
}

struct WireField {
    std::size_t offset = 0;
    WireKind kind = WireKind::U8;
};

/// One replicated component: its identity, its in-memory size, and every
/// field that crosses, in wire order.
struct ComponentCodec {
    schema::ComponentTypeId component;
    std::size_t size = 0;
    std::vector<WireField> fields;

    /// Bytes one value takes on the wire.
    [[nodiscard]] std::size_t wireSize() const noexcept;
    /// Whether every field lies inside the value.
    [[nodiscard]] bool valid() const noexcept;

    /// Writes the value at `value` (`size` bytes).
    [[nodiscard]] result::Status encode(const std::byte* value, network::Writer& writer) const;
    /// Reads one value's fields into `into` (`size` bytes); bytes of the
    /// value that no field covers are left as they were.
    [[nodiscard]] result::Status decode(network::Reader& reader, std::byte* into) const;
};

} // namespace rawframe::world_replication
