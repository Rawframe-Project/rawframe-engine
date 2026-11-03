// Coverage-guided fuzzing of a blockout document, which a mod's sources
// hold and the cook makes into a mesh. What the reader takes, the writer
// writes in a form it reads back to the same solids, and that form writes as
// itself; its mesh is made within the limits.

#include "rawframe/blockout/blockout.h"

#include <cstdlib>
#include <string>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kBlockout = blockout::parseBlockout(kText);
    if (!kBlockout.has_value()) {
        return 0;
    }
    const std::string kWritten = blockout::writeBlockout(*kBlockout);
    const auto kAgain = blockout::parseBlockout(kWritten);
    if (!kAgain.has_value() || *kAgain != *kBlockout || blockout::writeBlockout(*kAgain) != kWritten) {
        std::abort();
    }
    static_cast<void>(blockout::meshOf(*kBlockout));
    return 0;
}
