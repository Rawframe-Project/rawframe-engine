// Coverage-guided fuzzing of what a browser sends a server (D242): HTTP/3's
// streams and frames, the request that opens a WebTransport session, its
// QPACK field section and Huffman strings, and session datagrams. The input
// is read as a run of deliveries, each on one of the streams a client may
// open, so one input can open a control stream and a session and then
// break either.

#include "qpack.h"
#include "webtransport.h"

#include <algorithm>
#include <array>
#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    static_cast<void>(network_quic::decodeFieldSection(kBytes, network_quic::WebTransportServer::kLargestRequest));
    static_cast<void>(network_quic::decodeHuffman(kBytes, network_quic::WebTransportServer::kLargestRequest));

    // A client's first two-way streams and first one-way streams.
    constexpr std::array<std::uint64_t, 6> kStreams = {0, 4, 8, 2, 6, 10};
    network_quic::WebTransportServer server;
    std::size_t at = 0;
    while (at + 2 <= kBytes.size()) {
        const auto kHead = static_cast<std::uint8_t>(kBytes[at]);
        const std::size_t kLength = static_cast<std::uint8_t>(kBytes[at + 1]);
        at += 2;
        const std::span<const std::byte> kChunk = kBytes.subspan(at, std::min(kLength, kBytes.size() - at));
        at += kChunk.size();
        if ((kHead & 0x80U) != 0) {
            static_cast<void>(server.datagram(kChunk));
            continue;
        }
        const auto kArrived = server.receive(kStreams[kHead % kStreams.size()], kChunk, (kHead & 0x40U) != 0);
        if (kArrived.failure.has_value()) {
            break;
        }
    }
    return 0;
}
