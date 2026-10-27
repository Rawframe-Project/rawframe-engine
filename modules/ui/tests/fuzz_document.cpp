// Coverage-guided fuzzing of the UI documents a game or a mod carries:
// read, every node a component named once, each after the node it
// is inside.

#include "rawframe/ui/document.h"

#include <cstdlib>
#include <set>
#include <string>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kDocument = ui::readDocument(kText);
    if (!kDocument.has_value()) {
        return 0;
    }
    std::set<std::string> components;
    for (std::size_t index = 0; index < kDocument->nodes.size(); ++index) {
        const ui::DocumentNode& kNode = kDocument->nodes[index];
        if (!components.insert(kNode.component).second || (kNode.parent.has_value() && *kNode.parent >= index)) {
            std::abort();
        }
    }
    return 0;
}
