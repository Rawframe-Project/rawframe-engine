#include "rawframe/render/output.h"

#include <algorithm>

namespace rawframe::render {

namespace {

constexpr std::array<std::string_view, kOutputModes> kModeNames = {
    "sdr_srgb", "hdr_linear_fp16_rec709", "hdr10_pq_rec2020"};

} // namespace

std::optional<OutputMode> outputModeNamed(std::string_view name) noexcept {
    const auto kFound = std::ranges::find(kModeNames, name);
    if (kFound == kModeNames.end()) {
        return std::nullopt;
    }
    return static_cast<OutputMode>(kFound - kModeNames.begin());
}

std::string_view nameOf(OutputMode mode) noexcept {
    return kModeNames.at(static_cast<std::size_t>(mode));
}

std::string_view nameOf(OutputFallback fallback) noexcept {
    switch (fallback) {
    case OutputFallback::None:
        return "none";
    case OutputFallback::NotOffered:
        return "not_offered";
    case OutputFallback::HdrOff:
        return "hdr_off";
    case OutputFallback::NotDrawn:
        return "not_drawn";
    }
    return "none";
}

OutputRecord resolveOutput(const std::array<bool, kOutputModes>& offered,
                           const window::DisplayFacts& display,
                           OutputMode asked,
                           std::span<const OutputMode> drawn) noexcept {
    OutputRecord record{.offered = offered,
                        .referenceWhiteNits =
                            display.reported && display.sdrWhiteNits > 0 ? display.sdrWhiteNits : kReferenceWhiteNits,
                        .display = display};
    if (!offered.at(static_cast<std::size_t>(asked))) {
        record.fallback = OutputFallback::NotOffered;
    } else if (asked != OutputMode::SdrSrgb && !(display.reported && display.hdrOn)) {
        record.fallback = OutputFallback::HdrOff;
    } else if (!std::ranges::contains(drawn, asked)) {
        record.fallback = OutputFallback::NotDrawn;
    } else {
        record.active = asked;
    }
    return record;
}

} // namespace rawframe::render
