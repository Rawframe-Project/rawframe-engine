// Coverage-guided fuzzing of the JSON reader (D242), which reads what a
// Build, a mod, a save's manifest, and a game's documents hold. What the
// reader accepts must write back as its profile promises: canonical text
// byte for byte, and any accepted text as canonical text that reads again
// to the same writing.

#include "rawframe/document/json.h"

#include <cstdlib>
#include <string>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    if (const auto kValue = document::parse(kText); kValue.has_value()) {
        const std::string kWritten = document::write(*kValue);
        const auto kAgain = document::parseCanonical(kWritten);
        if (!kAgain.has_value() || document::write(*kAgain) != kWritten) {
            std::abort();
        }
        const auto kCompact = document::parse(document::writeCompact(*kValue));
        if (!kCompact.has_value() || document::write(*kCompact) != kWritten) {
            std::abort();
        }
    }
    if (const auto kValue = document::parseCanonical(kText); kValue.has_value() && document::write(*kValue) != kText) {
        std::abort();
    }
    if (const auto kValue = document::parseCanonicalRecord(kText); kValue.has_value()) {
        const auto kWritten = document::writeCanonicalRecord(*kValue);
        if (!kWritten.has_value() || *kWritten != kText) {
            std::abort();
        }
    }
    return 0;
}
