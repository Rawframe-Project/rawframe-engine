// How a node's fields lay out and look: sizes automatic at
// nought, a parent's AUTO stretching, a popup's margin taken as its gap, a
// number past its constants refused, and the parts a styled node sets.

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
    RAWFRAME_EXPECT(!layoutOf(NodeFields{.aspect = std::numeric_limits<float>::quiet_NaN()}).has_value());
}

RAWFRAME_TEST(ANodesFieldsLookAsTheySay) {
    const Look kLook = lookOf(NodeFields{.fill = 0x102030FF, .radius = 6});
    RAWFRAME_EXPECT(kLook.fill == 0x102030FF && kLook.radius == 6 && kLook.imageTint == 0xFFFFFFFF);
    RAWFRAME_EXPECT(ownPartsOf(NodeFields{.fill = 0x102030FF, .radius = 6}) ==
                    (static_cast<LookParts>(LookPart::Fill) | static_cast<LookParts>(LookPart::Radius)));
    RAWFRAME_EXPECT(ownPartsOf(NodeFields{}) == 0);
    RAWFRAME_EXPECT(textLookOf(NodeFields{}, Font{}).size == 16 &&
                    textLookOf(NodeFields{.textSize = 24}, Font{}).size == 24);
}
