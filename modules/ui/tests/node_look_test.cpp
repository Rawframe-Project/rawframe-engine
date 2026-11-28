// How a node's fields lay out and look: sizes automatic at
// nought, a parent's AUTO stretching, a popup's margin taken as its gap, a
// number past its constants refused, the parts a styled node sets, and
// its transparency as one less its opacity, held to its range.

#include "rawframe/test/test.h"
#include "rawframe/ui/node_look.h"

#include <limits>

using namespace rawframe;
using namespace rawframe::ui;

RAWFRAME_TEST(ANodesFieldsLayOutAsTheTreeTakesThem) {
    const std::optional<Layout> kPlain = layoutOf(NodeFields{});
    RAWFRAME_EXPECT(kPlain.has_value() && kPlain->width.automatic && kPlain->height.automatic &&
                    kPlain->alignItems == Align::Stretch);
    const std::optional<Layout> kSized =
        layoutOf(NodeFields{.widthScale = 0.5F, .heightOffset = 20, .margin = 4, .popup = 1});
    RAWFRAME_EXPECT(kSized.has_value() && !kSized->width.automatic && kSized->width.scale == 0.5F &&
                    kSized->height.offset == 20 && kSized->margin[0] == 0 && kSized->placement.absolute);
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.direction = 4}).has_value());
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.scroll = 4}).has_value());
    const std::optional<Layout> kBounded = layoutOf(NodeFields{
        .minWidthOffset = 40, .maxHeightScale = 0.5F, .aspect = 1.5F, .flexWrap = 2, .alignLines = 6, .lineGap = 3});
    RAWFRAME_EXPECT(kBounded.has_value() && kBounded->minWidth.offset == 40 && kBounded->minHeight.automatic &&
                    kBounded->maxHeight.scale == 0.5F && kBounded->maxWidth.automatic &&
                    kBounded->aspectRatio == 1.5F && kBounded->wrap && kBounded->wrapReverse &&
                    kBounded->alignLines == AlignLines::Stretch && kBounded->lineGap == 3);
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.flexWrap = 3}).has_value());
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.alignLines = 7}).has_value());
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.aspect = -1}).has_value());
    // An aspect past 4096 either way, which a side divided by it would make
    // endless; nought is none, and the bounds themselves are kept.
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.aspect = 4097}).has_value());
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.aspect = 1e-40F}).has_value());
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.aspect = 1.0F / 5000}).has_value());
    RAWFRAME_EXPECT(layoutOf(NodeFields{.aspect = 0}).has_value() && layoutOf(NodeFields{.aspect = 4096}).has_value() &&
                    layoutOf(NodeFields{.aspect = 1.0F / 4096}).has_value());
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.aspect = std::numeric_limits<float>::quiet_NaN()}).has_value());
}

RAWFRAME_TEST(ANodesFieldsLookAsTheySay) {
    const Look kLook = lookOf(NodeFields{.fill = 0x102030FF, .radius = 6});
    RAWFRAME_EXPECT(kLook.fill == 0x102030FF && kLook.radius == 6 && kLook.imageTint == 0xFFFFFFFF);
    RAWFRAME_EXPECT(ownPartsOf(NodeFields{.fill = 0x102030FF, .radius = 6}) ==
                    (static_cast<LookParts>(LookPart::Fill) | static_cast<LookParts>(LookPart::Radius)));
    RAWFRAME_EXPECT(ownPartsOf(NodeFields{}) == 0);
    // Nought is opaque and its own only when given.
    RAWFRAME_EXPECT(lookOf(NodeFields{}).opacity == 1 &&
                    lookOf(NodeFields{.transparency = 0.25F}).opacity == 0.75F &&
                    ownPartsOf(NodeFields{.transparency = 0.25F}) == static_cast<LookParts>(LookPart::Opacity));
    // A game's number past the range is held to it; not a number, opaque.
    RAWFRAME_EXPECT(lookOf(NodeFields{.transparency = 4}).opacity == 0 &&
                    lookOf(NodeFields{.transparency = -4}).opacity == 1 &&
                    lookOf(NodeFields{.transparency = std::numeric_limits<float>::quiet_NaN()}).opacity == 1);
    RAWFRAME_EXPECT(textLookOf(NodeFields{}, Font{}).size == 16 &&
                    textLookOf(NodeFields{.textSize = 24}, Font{}).size == 24);
}
