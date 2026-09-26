// Coverage-guided fuzzing of the input documents a game carries and a
// player's machine keeps (D243): action sets, and overrides read over the
// runners game's set. Overrides are read in their canonical text only, so
// what reads writes back to the very same text.

#include "rawframe/input/actions.h"
#include "rawframe/input/overrides.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

using namespace rawframe;

namespace {

/// The runners game's action set, read once.
const input::ActionSet& runners() {
    static const input::ActionSet kSet = [] {
        std::ifstream file{RAWFRAME_RUNNERS_ACTIONS, std::ios::binary};
        const std::string kText{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        auto read = input::readActionSet(kText);
        if (!read.has_value()) {
            std::abort();
        }
        return *std::move(read);
    }();
    return kSet;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    static_cast<void>(input::readActionSet(kText));
    const auto kOverrides = input::readOverrides(kText, runners(), "runners.actions");
    if (kOverrides.has_value() && input::writeOverrides(*kOverrides) != kText) {
        std::abort();
    }
    return 0;
}
