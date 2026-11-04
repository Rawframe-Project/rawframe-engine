#include "blockout_plan.h"

#include "rawframe/base/number_text.h"

#include <cmath>
#include <cstdlib>

namespace rawframe::studio {

std::optional<std::string> draggedPlace(std::string_view at, std::array<double, 2> meters) {
    const std::string kText{at};
    std::array<double, 3> place{};
    const char* reading = kText.c_str();
    for (double& each : place) {
        char* end = nullptr;
        each = std::strtod(reading, &end);
        if (end == reading) {
            return std::nullopt;
        }
        reading = end;
    }
    const auto kSnapped = [](double value) {
        return std::round(value * 4) / 4;
    };
    place[0] = kSnapped(place[0] + meters[0]);
    place[2] = kSnapped(place[2] + meters[1]);
    const auto kNumber = [](double value) {
        return base::numberText(value == 0 ? 0.0 : value);
    };
    return kNumber(place[0]) + " " + kNumber(place[1]) + " " + kNumber(place[2]);
}

} // namespace rawframe::studio
