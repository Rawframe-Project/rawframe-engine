#pragma once

// ADR-0047's display output, and each window's HDR capability record
// (ADR-0052, D365): the closed set of output modes, the ones a window's
// surface offers, the one used, the reference white SDR content is shown
// at, the display's facts, and a revision that moves whenever any of them
// does. A mode asked for that cannot be used resolves to SDR, surfaced
// (ADR-0047's fallback rule), never silently.

#include "rawframe/window/windows.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rawframe::render {

/// ADR-0047's closed output set: SDR, the sRGB transfer, Rec. 709
/// primaries; the general HDR path, half floats, linear transfer, Rec. 709
/// primaries, extended range; and HDR10, the PQ transfer, Rec. 2020
/// primaries, 10-bit class, never the default.
enum class OutputMode : std::uint8_t {
    SdrSrgb,
    HdrLinearFp16Rec709,
    Hdr10PqRec2020
};
inline constexpr std::size_t kOutputModes = 3;

/// The mode a configuration names (`sdr_srgb`, `hdr_linear_fp16_rec709`,
/// `hdr10_pq_rec2020`); none for another word.
[[nodiscard]] std::optional<OutputMode> outputModeNamed(std::string_view name) noexcept;
[[nodiscard]] std::string_view nameOf(OutputMode mode) noexcept;

/// Why the mode asked for is not the one used: none; the window's surface
/// does not offer it; the display's HDR output is off (an HDR mode needs
/// it on, and a display that does not tell is taken as off); or the
/// engine does not draw it yet.
enum class OutputFallback : std::uint8_t {
    None,
    NotOffered,
    HdrOff,
    NotDrawn
};
[[nodiscard]] std::string_view nameOf(OutputFallback fallback) noexcept;

/// BT.2408's reference white, nits: what SDR content is shown at in an
/// HDR mode where the platform tells no SDR white level (ADR-0047).
inline constexpr float kReferenceWhiteNits = 203;

/// A window's HDR capability record (ADR-0052). Consumers read it each
/// frame; a cached one is good only for its revision.
struct OutputRecord {
    /// By `OutputMode`, whether the window's surface offers it.
    std::array<bool, kOutputModes> offered{};
    OutputMode active = OutputMode::SdrSrgb;
    /// Why the mode asked for is not `active`.
    OutputFallback fallback = OutputFallback::None;
    /// The platform's SDR white level where it tells one, else
    /// `kReferenceWhiteNits`.
    float referenceWhiteNits = kReferenceWhiteNits;
    window::DisplayFacts display;
    /// One more each time anything above changes, from one.
    std::uint64_t revision = 0;

    /// The same record but for its revision.
    [[nodiscard]] bool sameAs(const OutputRecord& other) const noexcept {
        return offered == other.offered && active == other.active && fallback == other.fallback &&
               referenceWhiteNits == other.referenceWhiteNits && display == other.display;
    }
};

/// The record of a surface offering `offered` on a display of `display`,
/// `asked` resolved: used if offered, the display's HDR output on for an
/// HDR mode, and among `drawn`, the modes the engine draws; else SDR, with
/// why. Its revision is left nought.
[[nodiscard]] OutputRecord resolveOutput(const std::array<bool, kOutputModes>& offered,
                                         const window::DisplayFacts& display,
                                         OutputMode asked,
                                         std::span<const OutputMode> drawn) noexcept;

} // namespace rawframe::render
