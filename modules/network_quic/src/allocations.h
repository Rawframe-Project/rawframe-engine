#pragma once

// What MsQuic holds on the heap (SPEC-0013's network/provider memory,
// D234): its allocations are counted where the linker routes them
// (`--wrap`), so the vendored library is not changed. Only where the
// build wraps them; elsewhere nothing is counted.

#include <cstdint>
#include <optional>

namespace rawframe::network_quic {

/// Bytes MsQuic has allocated and not freed, as the allocator sizes them.
[[nodiscard]] std::optional<std::uint64_t> quicHeapBytes() noexcept;

} // namespace rawframe::network_quic
