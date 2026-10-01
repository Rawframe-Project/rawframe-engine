// ADR-0047's output modes resolved against a window's HDR capability
// record (ADR-0052, D365): the mode asked for used only where the surface
// offers it, the display's HDR output is on, and the engine draws it;
// otherwise SDR, with why; and the reference white the platform's where it
// tells one.

#include "rawframe/render/output.h"
#include "rawframe/test/test.h"

#include <array>

using namespace rawframe;
using render::OutputFallback;
using render::OutputMode;

RAWFRAME_TEST(AnOutputModeIsUsedOnlyWhereItCanBe) {
    constexpr std::array<bool, render::kOutputModes> kSdrOnly = {true, false, false};
    constexpr std::array<bool, render::kOutputModes> kAll = {true, true, true};
    constexpr std::array<OutputMode, 1> kSdrDrawn = {OutputMode::SdrSrgb};
    constexpr std::array<OutputMode, 2> kLinearDrawn = {OutputMode::SdrSrgb, OutputMode::HdrLinearFp16Rec709};
    const window::DisplayFacts kSilent;
    const window::DisplayFacts kHdr{.reported = true, .hdrOn = true, .peakNits = 1000, .sdrWhiteNits = 240};
    const window::DisplayFacts kHdrOff{.reported = true, .hdrOn = false, .peakNits = 400, .sdrWhiteNits = 0};
    // SDR asked: used, on any display, at BT.2408's reference white where
    // the platform tells none.
    const auto kSdr = render::resolveOutput(kSdrOnly, kSilent, OutputMode::SdrSrgb, kSdrDrawn);
    RAWFRAME_EXPECT(kSdr.active == OutputMode::SdrSrgb && kSdr.fallback == OutputFallback::None &&
                    kSdr.referenceWhiteNits == render::kReferenceWhiteNits && kSdr.revision == 0);
    // HDR asked of a surface that does not offer it, of a display whose
    // HDR is off or that does not tell, and of an engine that does not draw
    // it: SDR each time, with why.
    RAWFRAME_EXPECT(render::resolveOutput(kSdrOnly, kHdr, OutputMode::HdrLinearFp16Rec709, kLinearDrawn).fallback ==
                    OutputFallback::NotOffered);
    RAWFRAME_EXPECT(render::resolveOutput(kAll, kHdrOff, OutputMode::HdrLinearFp16Rec709, kLinearDrawn).fallback ==
                    OutputFallback::HdrOff);
    RAWFRAME_EXPECT(render::resolveOutput(kAll, kSilent, OutputMode::Hdr10PqRec2020, kLinearDrawn).fallback ==
                    OutputFallback::HdrOff);
    const auto kNotDrawn = render::resolveOutput(kAll, kHdr, OutputMode::Hdr10PqRec2020, kLinearDrawn);
    RAWFRAME_EXPECT(kNotDrawn.active == OutputMode::SdrSrgb && kNotDrawn.fallback == OutputFallback::NotDrawn);
    // Where all holds, the mode asked for, SDR white the platform's.
    const auto kUsed = render::resolveOutput(kAll, kHdr, OutputMode::HdrLinearFp16Rec709, kLinearDrawn);
    RAWFRAME_EXPECT(kUsed.active == OutputMode::HdrLinearFp16Rec709 && kUsed.fallback == OutputFallback::None &&
                    kUsed.referenceWhiteNits == 240 && kUsed.display == kHdr);
    // A record is the same but for its revision only when nothing else moved.
    auto revised = kUsed;
    revised.revision = 7;
    auto moved = kUsed;
    moved.display.sdrWhiteNits = 300;
    RAWFRAME_EXPECT(revised.sameAs(kUsed) && !moved.sameAs(kUsed));
    // Named as ADR-0047 names them.
    RAWFRAME_EXPECT(render::outputModeNamed("hdr10_pq_rec2020") == OutputMode::Hdr10PqRec2020 &&
                    render::nameOf(OutputMode::HdrLinearFp16Rec709) == "hdr_linear_fp16_rec709" &&
                    !render::outputModeNamed("scRGB").has_value());
}
