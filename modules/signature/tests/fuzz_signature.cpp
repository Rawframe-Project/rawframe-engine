// Coverage-guided fuzzing of the signature records a Build carries (D242):
// the detached envelope and the publisher's key set. Both readers take the
// canonical record only, so what they accept writes back to the very same
// text.

#include "rawframe/signature/signature.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    if (const auto kEnvelope = signature::readEnvelope(kText);
        kEnvelope.has_value() && signature::writeEnvelope(*kEnvelope) != kText) {
        std::abort();
    }
    if (const auto kKeys = signature::readPublisherKeySet(kText); kKeys.has_value()) {
        const auto kWritten = signature::writePublisherKeySet(*kKeys);
        if (!kWritten.has_value() || *kWritten != kText) {
            std::abort();
        }
    }
    return 0;
}
