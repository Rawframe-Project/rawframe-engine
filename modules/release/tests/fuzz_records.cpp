// Coverage-guided fuzzing of SPEC-0020's records as a launcher reads them
// from any mirror (D424): a ReleaseRecord or a ChannelPointer read from
// hostile bytes is exact, so what reads writes back to the same bytes.

#include "rawframe/release/release.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    if (const auto kRecord = release::readRelease(kText); kRecord.has_value()) {
        const auto kWritten = release::writeRelease(*kRecord);
        if (!kWritten.has_value() || *kWritten != kText) {
            std::abort();
        }
    }
    if (const auto kPointer = release::readPointer(kText); kPointer.has_value()) {
        const auto kWritten = release::writePointer(*kPointer);
        if (!kWritten.has_value() || *kWritten != kText) {
            std::abort();
        }
    }
    return 0;
}
