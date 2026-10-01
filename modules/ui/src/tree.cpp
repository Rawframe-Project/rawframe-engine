#include "rawframe/ui/tree.h"

#include "rawframe/ui/errors.h"

#include <maul-ui/context.h>
#include <maul-ui/draw.h>
#include <maul-ui/layout.h>
#include <maul-ui/node.h>
#include <maul-ui/style.h>
#include <maul-ui/visual.h>
#include <string_view>

namespace rawframe::ui {

namespace {

std::unexpected<result::Error> refuse(UiError error, std::string_view why) {
    const result::ErrorClass kClass = error == UiError::Capacity ? result::ErrorClass::ResourceExhausted
                                      : error == UiError::Stale  ? result::ErrorClass::NotFound
                                                                 : result::ErrorClass::InvalidArgument;
    return std::unexpected<result::Error>{result::fail(kClass, kUiDomain, code(error), why).error()};
}

/// Maul UI's answer as this module's.
result::Status checked(muiResult outcome, std::string_view why) {
    switch (outcome) {
    case mui_success:
        return {};
    case mui_errorCapacity:
        return refuse(UiError::Capacity, why);
    case mui_errorStale:
        return refuse(UiError::Stale, why);
    default:
        return refuse(UiError::Invalid, why);
    }
}

muiNodeId idOf(Node node) noexcept {
    return muiNodeId{.index1 = node.index1, .generation = node.generation};
}

muiDimension dimensionOf(Dimension dimension) noexcept {
    return dimension.automatic
               ? muiDimension{.scale = 0, .offset = 0, .kind = mui_dimensionAuto}
               : muiDimension{.scale = dimension.scale, .offset = dimension.offset, .kind = mui_dimensionValue};
}

muiEdges edgesOf(const std::array<float, 4>& sides) noexcept {
    return muiEdges{.start = sides[0], .end = sides[1], .top = sides[2], .bottom = sides[3]};
}

/// 0xRRGGBBAA as Maul UI's color: sRGB with straight alpha, 0 to 1.
muiColor colorOf(std::uint32_t color) noexcept {
    const auto kChannel = [color](unsigned shift) {
        return static_cast<float>((color >> shift) & 0xFFU) / 255.0F;
    };
    return muiColor{.r = kChannel(24), .g = kChannel(16), .b = kChannel(8), .a = kChannel(0)};
}

std::array<float, 4> linearOf(const muiLinearColor& color) noexcept {
    return {color.r, color.g, color.b, color.a};
}

Rect rectOf(const muiRect& rect) noexcept {
    return Rect{.x = rect.x, .y = rect.y, .width = rect.width, .height = rect.height};
}

std::array<float, 4> cornersOf(const muiCorners& corners) noexcept {
    return {corners.topLeft, corners.topRight, corners.bottomRight, corners.bottomLeft};
}

muiAlign alignOf(Align align) noexcept {
    switch (align) {
    case Align::Auto:
        return mui_alignAuto;
    case Align::Stretch:
        return mui_alignStretch;
    case Align::Start:
        return mui_alignStart;
    case Align::End:
        return mui_alignEnd;
    case Align::Center:
        return mui_alignCenter;
    }
    return mui_alignAuto;
}

} // namespace

struct Tree::State {
    muiContext* context = nullptr;

    ~State() {
        muiDestroyContext(context);
    }
};

Tree::Tree(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Tree::~Tree() = default;

result::Result<std::unique_ptr<Tree>> Tree::create(std::uint32_t maximumNodes) {
    muiContextDef def = muiDefaultContextDef();
    def.limits.nodes = maximumNodes;
    auto state = std::make_unique<State>();
    RAWFRAME_TRY(checked(muiCreateContext(&def, &state->context), "a UI tree could not be made"));
    return std::unique_ptr<Tree>{new Tree{std::move(state)}};
}

result::Result<Node> Tree::add(std::uint64_t key) {
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = key;
    muiNodeId made{};
    RAWFRAME_TRY(checked(muiCreateNode(state_->context, &def, &made), "a UI node could not be added"));
    return Node{.index1 = made.index1, .generation = made.generation};
}

result::Status Tree::attach(Node parent, Node child) {
    return checked(muiNode_InsertChild(state_->context, idOf(parent), idOf(child), muiNodeId{}),
                   "a UI node could not be attached");
}

result::Status Tree::detach(Node node) {
    return checked(muiNode_Detach(state_->context, idOf(node)), "a UI node could not be detached");
}

result::Status Tree::remove(Node node) {
    return checked(muiDestroyNode(state_->context, idOf(node)), "a UI node could not be removed");
}

bool Tree::contains(Node node) const noexcept {
    return muiNode_IsValid(state_->context, idOf(node));
}

std::uint64_t Tree::keyOf(Node node) const noexcept {
    return muiNode_GetHostKey(state_->context, idOf(node));
}

result::Status Tree::setLayout(Node node, const Layout& layout) {
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.sizing.width = dimensionOf(layout.width);
    style.sizing.height = dimensionOf(layout.height);
    style.container.direction = static_cast<muiFlexDirection>(layout.direction);
    style.container.justify = static_cast<muiJustify>(layout.justify);
    style.container.alignItems = alignOf(layout.alignItems);
    style.container.rowGap = layout.gap;
    style.container.columnGap = layout.gap;
    style.item.grow = layout.grow;
    style.item.shrink = layout.shrink;
    style.item.alignSelf = alignOf(layout.alignSelf);
    style.padding = edgesOf(layout.padding);
    style.margin = edgesOf(layout.margin);
    style.border = edgesOf(layout.border);
    if (layout.placement.absolute) {
        style.placement.position = mui_positionAbsolute;
        style.placement.inset.start = dimensionOf(layout.placement.x);
        style.placement.inset.top = dimensionOf(layout.placement.y);
        style.placement.anchorX = layout.placement.anchorX;
        style.placement.anchorY = layout.placement.anchorY;
    }
    return checked(muiNode_SetLayoutStyle(state_->context, idOf(node), &style), "a UI node's layout was refused");
}

result::Status Tree::layOut(Node root, float width, float height) {
    const muiLayoutInput kInput{
        .availableWidth = width, .availableHeight = height, .measure = nullptr, .measureUser = nullptr, .timeNs = 0};
    return checked(muiComputeLayout(state_->context, idOf(root), &kInput), "a UI tree could not be laid out");
}

Rect Tree::rectOf(Node node) const noexcept {
    return ui::rectOf(muiNode_GetRect(state_->context, idOf(node)));
}

result::Status Tree::setLook(Node node, const Look& look) {
    muiVisualStyle style = muiDefaultVisualStyle();
    style.background = colorOf(look.fill);
    const muiColor kBorder = colorOf(look.borderColor);
    style.borderColor = muiEdgeColors{.start = kBorder, .end = kBorder, .top = kBorder, .bottom = kBorder};
    const muiDimension kRadius{.scale = 0, .offset = look.radius, .kind = mui_dimensionValue};
    style.radius = muiCornerRadii{.topStart = kRadius, .topEnd = kRadius, .bottomEnd = kRadius, .bottomStart = kRadius};
    style.clip = look.clip;
    style.image = look.image;
    style.imageSlice = muiEdges{.start = look.imageSlice[3],
                                .end = look.imageSlice[1],
                                .top = look.imageSlice[0],
                                .bottom = look.imageSlice[2]};
    style.imageTint = colorOf(look.imageTint);
    return checked(muiNode_SetVisualValues(state_->context, idOf(node), &style, MUI_VISUAL_PROPERTIES),
                   "a UI node's look was refused");
}

result::Status Tree::draw(Node root, float scale, DrawList& into) {
    const muiDrawInput kInput{.surface = 0, .scale = scale, .paint = nullptr, .paintUser = nullptr};
    RAWFRAME_TRY(checked(muiBuildDrawList(state_->context, idOf(root), &kInput), "a UI tree could not be drawn"));
    muiDrawList list{};
    RAWFRAME_TRY(checked(muiGetDrawList(state_->context, &list), "a UI tree's drawing could not be read"));
    into.boxes.clear();
    into.images.clear();
    into.commands.clear();
    into.clips.clear();
    into.skipped = 0;
    into.scale = scale;
    for (std::uint32_t at = 0; at < list.clipCount; ++at) {
        const muiDrawClip& kClip = list.clips[at];
        into.clips.push_back(Clip{.rect = ui::rectOf(kClip.rect),
                                  .radii = cornersOf(kClip.radii),
                                  .parent = kClip.parent,
                                  .invert = kClip.invert != 0});
    }
    for (std::uint32_t at = 0; at < list.commandCount; ++at) {
        const muiDrawCommand& kCommand = list.commands[at];
        if (kCommand.kind == mui_drawImage && kCommand.transform == 0) {
            const muiDrawImage& kImage = kCommand.image;
            into.commands.push_back(
                DrawCommand{.kind = DrawCommand::Kind::Image, .index = static_cast<std::uint32_t>(into.images.size())});
            into.images.push_back(
                Image{.rect = ui::rectOf(kImage.rect),
                      .image = kImage.image,
                      .uv = ui::rectOf(kImage.uv),
                      .slice = {kImage.slice.top, kImage.slice.right, kImage.slice.bottom, kImage.slice.left},
                      .tint = linearOf(kImage.tint),
                      .clip = kCommand.clip});
            continue;
        }
        if (kCommand.kind != mui_drawBox || kCommand.transform != 0 || kCommand.box.gradient != 0) {
            ++into.skipped;
            continue;
        }
        into.commands.push_back(
            DrawCommand{.kind = DrawCommand::Kind::Box, .index = static_cast<std::uint32_t>(into.boxes.size())});
        const muiDrawBox& kBox = kCommand.box;
        into.boxes.push_back(Box{.rect = ui::rectOf(kBox.rect),
                                 .radii = cornersOf(kBox.radii),
                                 .fill = linearOf(kBox.fill),
                                 .borderWidths = {kBox.borderWidths.top,
                                                  kBox.borderWidths.right,
                                                  kBox.borderWidths.bottom,
                                                  kBox.borderWidths.left},
                                 .borderColors = {linearOf(kBox.borderColors[0]),
                                                  linearOf(kBox.borderColors[1]),
                                                  linearOf(kBox.borderColors[2]),
                                                  linearOf(kBox.borderColors[3])},
                                 .clip = kCommand.clip});
    }
    return {};
}

} // namespace rawframe::ui
