// Coverage-guided fuzzing of what a peer sends before and around admission:
// a datagram record, a stream's preface, frames read one after another
// from a stream, and the admission, close and termination payloads. Each
// accepted payload must also write back to bytes that read to the same
// payload, so a reader and its writer cannot drift apart.

#include "rawframe/network/admission.h"
#include "rawframe/network/close.h"
#include "rawframe/network/wire.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <span>

using namespace rawframe;

namespace {

/// Encodes a decoded value and decodes it again: the second encoding must
/// be the first, byte for byte.
template <typename Decode, typename Encode>
void roundTrips(std::span<const std::byte> payload, Decode decode, Encode encode) {
    const auto kFirst = decode(payload);
    if (!kFirst.has_value()) {
        return;
    }
    std::array<std::byte, 8192> once{};
    network::Writer writer{once};
    if (!encode(writer, *kFirst).has_value()) {
        return;
    }
    const auto kSecond = decode(writer.written());
    if (!kSecond.has_value()) {
        std::abort();
    }
    std::array<std::byte, 8192> twice{};
    network::Writer again{twice};
    if (!encode(again, *kSecond).has_value() || !std::ranges::equal(again.written(), writer.written())) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    static_cast<void>(network::readDatagram(kBytes, 1200));
    // A preface's varints are canonical, so one read writes back exactly.
    network::Reader preface{kBytes};
    if (const auto kPreface = network::readPreface(preface); kPreface.has_value()) {
        std::array<std::byte, 64> written{};
        network::Writer writer{written};
        const std::size_t kRead = kBytes.size() - preface.remaining();
        if (!network::writePreface(writer, *kPreface).has_value() ||
            !std::ranges::equal(writer.written(), kBytes.first(kRead))) {
            std::abort();
        }
    }
    network::Reader reader{kBytes};
    while (network::readFrame(reader, 4096).has_value()) {
    }
    roundTrips(kBytes, network::decodeHello, network::encodeHello);
    roundTrips(kBytes, network::decodeAccept, network::encodeAccept);
    roundTrips(kBytes, network::decodeReject, network::encodeReject);
    roundTrips(kBytes, network::decodeGracefulClose, network::encodeGracefulClose);
    roundTrips(kBytes, network::decodeTermination, network::encodeTermination);
    return 0;
}
