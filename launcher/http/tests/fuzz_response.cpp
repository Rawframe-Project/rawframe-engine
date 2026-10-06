// The response reader over any bytes (D414): never a crash or a read out of
// bounds, and whatever it takes it takes in order.

#include "rawframe/http/response.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    // The first byte splits the rest in two reads and picks a ceiling.
    const std::size_t kSplit = size > 1 ? data[0] % size : 0;
    rawframe::http::ResponseReader reader{static_cast<std::uint64_t>(data[0]) * 64};
    const auto kBytes = std::as_bytes(std::span{data + 1, size - 1});
    std::size_t at = 0;
    for (const std::size_t kEnd : {std::min(kSplit, kBytes.size()), kBytes.size()}) {
        while (at < kEnd && !reader.whole()) {
            const auto kTaken = reader.read(kBytes.subspan(at, kEnd - at));
            if (!kTaken.has_value()) {
                return 0;
            }
            at += *kTaken;
        }
    }
    if (!reader.whole()) {
        static_cast<void>(reader.ended());
    }
    return 0;
}
