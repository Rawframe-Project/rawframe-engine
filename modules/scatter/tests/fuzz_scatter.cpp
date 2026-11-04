// Coverage-guided fuzzing of a scatter document and its placed form, which
// a mod's Build can hold and the Runtime reads. The input is a scatter, then
// after a nought byte the terrain it stands on. What either reader takes,
// the writer writes in a form it reads back to the same value, and that
// form writes as itself; the copies a scatter places are at most its count
// and read back to the bit.

#include "rawframe/scatter/scatter.h"
#include "rawframe/terrain/terrain.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

using namespace rawframe;

namespace {

template <typename Read, typename Write> void again(std::string_view text, Read read, Write write) {
    const auto kRead = read(text, {});
    if (!kRead.has_value()) {
        return;
    }
    const std::string kWritten = write(*kRead);
    const auto kAgain = read(kWritten, {});
    if (!kAgain.has_value() || *kAgain != *kRead || write(*kAgain) != kWritten) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kInput{reinterpret_cast<const char*>(data), size};
    const std::size_t kSplit = std::min(kInput.find('\0'), kInput.size());
    const std::string_view kText = kInput.substr(0, kSplit);
    again(kText, scatter::parsePlaced, scatter::writePlaced);
    again(kText, scatter::parseScatter, scatter::writeScatter);
    const auto kScatter = scatter::parseScatter(kText);
    if (!kScatter.has_value()) {
        return 0;
    }
    std::optional<terrain::Terrain> ground;
    if (kSplit < kInput.size()) {
        auto parsed = terrain::parseTerrain(kInput.substr(kSplit + 1));
        if (parsed.has_value()) {
            ground = std::move(*parsed);
        }
    }
    const auto kPlacements = scatter::placements(*kScatter, ground.has_value() ? &*ground : nullptr);
    if (!kPlacements.has_value()) {
        return 0;
    }
    const scatter::Placed kPlaced{.prefab = kScatter->prefab, .placements = *kPlacements};
    const auto kRead = scatter::parsePlaced(scatter::writePlaced(kPlaced));
    if (kPlacements->size() > kScatter->count || !kRead.has_value() || *kRead != kPlaced) {
        std::abort();
    }
    return 0;
}
