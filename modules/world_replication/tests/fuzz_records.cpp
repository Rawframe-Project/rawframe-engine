// Coverage-guided fuzzing of replication's records (D242): what a client
// sends a server (input windows, state acknowledgements, checksums, the
// perception it claims) and what a server sends a client (mappings, pace,
// state). Every decoder here is exact, so a payload any of them accepts
// must write back to the very same bytes.

#include "rawframe/world_replication/records.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <span>

using namespace rawframe;

namespace {

template <typename Decode, typename Encode>
void exact(std::span<const std::byte> payload, Decode decode, Encode encode) {
    const auto kDecoded = decode(payload);
    if (!kDecoded.has_value()) {
        return;
    }
    std::array<std::byte, 8192> written{};
    network::Writer writer{written};
    if (!encode(writer, *kDecoded).has_value() || !std::ranges::equal(writer.written(), payload)) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    using namespace world_replication;
    exact(kBytes, decodeInputWindow, encodeInputWindow);
    exact(kBytes, decodeStateAck, encodeStateAck);
    exact(kBytes, decodeChecksum, encodeChecksum);
    exact(kBytes, decodePerception, encodePerception);
    exact(kBytes, decodeMapping, encodeMapping);
    exact(kBytes, decodePace, encodePace);
    // A state payload: its header, then as many record heads as it reads.
    network::Reader reader{kBytes};
    if (decodeStateHeader(reader).has_value()) {
        while (decodeStateRecordHead(reader, 64).has_value()) {
        }
    }
    return 0;
}
