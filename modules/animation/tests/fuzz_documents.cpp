// Coverage-guided fuzzing of the animation documents a mod's Build can hold
// (D243): clips, graphs, masks, and skeletons. Each reader takes the
// canonical form only, so what it accepts writes back to the very same
// text.

#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/mask.h"
#include "rawframe/animation/skeleton.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

namespace {

template <typename Read, typename Write> void exact(std::string_view text, Read read, Write write) {
    const auto kRead = read(text, {});
    if (!kRead.has_value()) {
        return;
    }
    const auto kWritten = write(*kRead, {});
    if (!kWritten.has_value() || *kWritten != text) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    exact(kText, animation::readClip, animation::writeClip);
    exact(kText, animation::readGraph, animation::writeGraph);
    exact(kText, animation::readMask, animation::writeMask);
    exact(kText, animation::readSkeleton, animation::writeSkeleton);
    return 0;
}
