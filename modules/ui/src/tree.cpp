#include "rawframe/ui/tree.h"

#include "rawframe/ui/errors.h"

#include <maul-ui/context.h>
#include <maul-ui/layout.h>
#include <maul-ui/node.h>
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
    return checked(muiNode_SetLayoutStyle(state_->context, idOf(node), &style), "a UI node's layout was refused");
}

result::Status Tree::layOut(Node root, float width, float height) {
    const muiLayoutInput kInput{
        .availableWidth = width, .availableHeight = height, .measure = nullptr, .measureUser = nullptr, .timeNs = 0};
    return checked(muiComputeLayout(state_->context, idOf(root), &kInput), "a UI tree could not be laid out");
}

Rect Tree::rectOf(Node node) const noexcept {
    const muiRect kRect = muiNode_GetRect(state_->context, idOf(node));
    return Rect{.x = kRect.x, .y = kRect.y, .width = kRect.width, .height = kRect.height};
}

} // namespace rawframe::ui
