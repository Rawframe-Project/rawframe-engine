// Coverage-guided fuzzing of the audio documents a Build carries (D242): a
// mixer layout, and sound declarations read against the runners game's
// layout. Neither has a writer, so the search looks for what breaks the
// readers themselves.

#include "rawframe/audio/layout.h"
#include "rawframe/audio/sound.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using namespace rawframe;

namespace {

/// The runners game's mixer layout, read once.
const audio::Layout& runners() {
    static const audio::Layout kLayout = [] {
        std::ifstream file{RAWFRAME_RUNNERS_MIXER, std::ios::binary};
        const std::string kText{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        auto read = audio::readLayout(kText);
        if (!read.has_value()) {
            std::abort();
        }
        return *std::move(read);
    }();
    return kLayout;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    static_cast<void>(audio::readLayout(kText));
    static_cast<void>(audio::readSound(kText, runners()));
    return 0;
}
