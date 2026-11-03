// Coverage-guided fuzzing of a terrain document, which a mod's Build can
// hold and the Runtime reads beneath a scatter. What the reader takes, the
// writer writes in a form it reads back to the same terrain, and that form
// writes as itself; its mesh is made, and its ground is there to stand on
// within the grid.

#include "rawframe/terrain/terrain.h"

#include <cstdlib>
#include <string>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kTerrain = terrain::parseTerrain(kText);
    if (!kTerrain.has_value()) {
        return 0;
    }
    const std::string kWritten = terrain::writeTerrain(*kTerrain);
    const auto kAgain = terrain::parseTerrain(kWritten);
    if (!kAgain.has_value() || *kAgain != *kTerrain || terrain::writeTerrain(*kAgain) != kWritten) {
        std::abort();
    }
    static_cast<void>(terrain::meshOf(*kTerrain));
    const double kWidth = (kTerrain->columns - 1) * kTerrain->cell;
    const double kDepth = (kTerrain->rows - 1) * kTerrain->cell;
    for (const double kAt : {0.0, 0.5}) {
        if (!terrain::heightAt(*kTerrain, kWidth * kAt, kDepth * kAt).has_value()) {
            std::abort();
        }
        static_cast<void>(terrain::layerAt(*kTerrain, kWidth * kAt, kDepth * kAt));
    }
    return 0;
}
