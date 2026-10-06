// Coverage-guided fuzzing of the style documents a game or a mod carries
// (D431): read, and when read, made into a tree's classes, which must take
// every value the reader let through.

#include "rawframe/ui/styles.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kSheet = ui::readStyles(kText);
    if (!kSheet.has_value()) {
        return 0;
    }
    auto tree = ui::Tree::create(16);
    if (tree.has_value() && !ui::addStyles(**tree, *kSheet).has_value()) {
        std::abort();
    }
    return 0;
}
