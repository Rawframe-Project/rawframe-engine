#pragma once

// A UI tree (ADR-0034, SPEC-0030, D374): Maul UI's retained core owned for
// the engine, its nodes found by a key the owner chooses (an entity's), laid
// out by SPEC-0030's Scale+Offset sizing and flexbox algebra into
// rectangles relative to their parents, and drawn as SPEC-0032's
// draw-command list. A node and a subtree that did not change are not laid
// out or painted again. Maul UI's types stay inside this module.

#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace rawframe::ui {

/// A size along one axis (SPEC-0030's Scale+Offset): automatic (sized by
/// the layout rules), or `scale` of the parent's content extent plus
/// `offset` pixels.
struct Dimension {
    bool automatic = true;
    float scale = 0;
    float offset = 0;
};

/// A fixed size of `pixels`.
[[nodiscard]] constexpr Dimension pixels(float value) noexcept {
    return {.automatic = false, .scale = 0, .offset = value};
}
/// `share` of the parent's content extent.
[[nodiscard]] constexpr Dimension share(float value) noexcept {
    return {.automatic = false, .scale = value, .offset = 0};
}

enum class Direction : std::uint8_t {
    Row,
    RowReverse,
    Column,
    ColumnReverse
};

enum class Justify : std::uint8_t {
    Start,
    End,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

/// How children sit on the cross axis; `Auto` (a child's own, as its
/// parent's) is not a container's.
enum class Align : std::uint8_t {
    Auto,
    Stretch,
    Start,
    End,
    Center
};

/// Where an absolute node goes (SPEC-0030's anchor point): out of its
/// parent's flex layout, its start and top edges at `x` and `y` from the
/// start and top of its parent's padding box (automatic for where it would
/// sit as its parent's only child), then moved back by `anchorX` and
/// `anchorY` of its own size, 0 to 1, so 0.5, 0.5 centers it there.
struct Placement {
    bool absolute = false;
    Dimension x;
    Dimension y;
    float anchorX = 0;
    float anchorY = 0;
};

/// A node's authored layout: its size, how it lays out its children (a
/// flex container), and how it takes part in its parent's (a flex item, or
/// placed apart from them). Sides are start, end, top, bottom: start and
/// end follow the inline direction.
struct Layout {
    Dimension width;
    Dimension height;
    Direction direction = Direction::Row;
    Justify justify = Justify::Start;
    Align alignItems = Align::Stretch;
    float gap = 0;
    float grow = 0;
    float shrink = 1;
    Align alignSelf = Align::Auto;
    std::array<float, 4> padding{};
    std::array<float, 4> margin{};
    /// The border's widths, inside the border box.
    std::array<float, 4> border{};
    Placement placement;
};

/// A shadow of a node's rounded box, as CSS's box-shadow (D381): its color,
/// 0xRRGGBBAA sRGB with straight alpha (nought casts none), its offset in
/// pixels, the distance it blurs over, and how far it grows past the box.
struct ShadowLook {
    std::uint32_t color = 0;
    float x = 0;
    float y = 0;
    float blur = 0;
    float spread = 0;
};

/// A gradient painted over a node's fill (D382): linear along a line
/// through its center at `angle` degrees clockwise from toward the top, as
/// CSS's linear-gradient, or radial outward from its center to its
/// farthest corner; two to four stops in order of position, nought to one,
/// each a color 0xRRGGBBAA sRGB with straight alpha.
struct GradientLook {
    enum class Kind : std::uint8_t {
        None,
        Linear,
        Radial
    };
    Kind kind = Kind::None;
    float angle = 180;
    std::array<std::uint32_t, 4> colors{};
    std::array<float, 4> positions{};
    std::uint32_t stops = 0;
};

/// A node's look (SPEC-0032's box): its fill and its border's color, each
/// 0xRRGGBBAA, sRGB with straight alpha (nought draws nothing); its
/// corners' radius in pixels, held to half its shorter side; whether it
/// clips its children to its rounded border box; and an image filling its
/// border box over its fill (D378): the owner's key for it (nought for
/// none), insets of its nine-slice center in its pixels (top, right,
/// bottom, left; all nought stretches it whole), and its tint.
struct Look {
    std::uint32_t fill = 0;
    std::uint32_t borderColor = 0;
    float radius = 0;
    bool clip = false;
    std::uint64_t image = 0;
    std::array<float, 4> imageSlice{};
    std::uint32_t imageTint = 0xFFFFFFFF;
    /// Cast outside its border box, and inside its padding box (D381).
    ShadowLook outerShadow;
    ShadowLook innerShadow;
    GradientLook gradient;
};

/// A node's border box from the last layout that reached it, relative to
/// its parent's: x right, y down, in pixels.
struct Rect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

/// A rounded box to draw (SPEC-0032), in pixels from the root's top left,
/// y down: its border box; its corners' radii (top left, then clockwise);
/// its fill, linear light with premultiplied alpha; its borders' widths
/// and colors (top, right, bottom, left), inside the box; and the clip it
/// is drawn in, an index of its list's clips, nought for none; and the
/// gradient over its fill, an index of the list's gradients, nought for
/// none (D382).
struct Box {
    Rect rect;
    std::array<float, 4> radii{};
    std::array<float, 4> fill{};
    std::array<float, 4> borderWidths{};
    std::array<std::array<float, 4>, 4> borderColors{};
    std::uint32_t clip = 0;
    std::uint32_t gradient = 0;
};

/// A gradient of a list (D382): its kind, linear or radial; for a linear
/// one, its angle in degrees clockwise from toward the top; and its stops,
/// colors linear with premultiplied alpha at positions nought to one, which
/// it moves between through premultiplied Oklab.
struct Gradient {
    GradientLook::Kind kind = GradientLook::Kind::Linear;
    float angle = 0;
    std::uint32_t stops = 0;
    std::array<std::array<float, 4>, 4> colors{};
    std::array<float, 4> positions{};
};

/// An image to draw (SPEC-0032, D378): its owner's key, the part of it
/// (`uv`, nought to one from its top left) drawn into `rect`, its nine-slice
/// insets in its own pixels (top, right, bottom, left), whose corners keep
/// their size at one logical pixel a pixel; its tint, linear with
/// premultiplied alpha; and its clip.
struct Image {
    Rect rect;
    std::uint64_t image = 0;
    Rect uv;
    std::array<float, 4> slice{};
    std::array<float, 4> tint{};
    std::uint32_t clip = 0;
};

/// A shadow to draw (SPEC-0032, D381): of the rounded box `rect` and
/// `radii`, outside it or, inset, inside it; offset, grown by `spread`, and
/// blurred over `blur` pixels (a Gaussian of half that deviation); its
/// color linear with premultiplied alpha; and its clip.
struct Shadow {
    Rect rect;
    std::array<float, 4> radii{};
    std::array<float, 4> color{};
    float x = 0;
    float y = 0;
    float blur = 0;
    float spread = 0;
    bool inset = false;
    std::uint32_t clip = 0;
};

/// A command of a list in paint order: a box, an image, or a shadow, by its
/// index in the list's own.
struct DrawCommand {
    enum class Kind : std::uint8_t {
        Box,
        Image,
        Shadow
    };
    Kind kind = Kind::Box;
    std::uint32_t index = 0;
};

/// A clip: drawing kept inside the rounded rectangle (outside it, when
/// inverted) and inside its parent, an index of the list's clips.
struct Clip {
    Rect rect;
    std::array<float, 4> radii{};
    std::uint32_t parent = 0;
    bool invert = false;
};

/// What a tree draws: its boxes, images, and shadows, `commands` saying
/// their paint order, and its clips, the first a placeholder for none, in
/// logical pixels, `scale` device pixels each. Commands generation 1 does
/// not draw yet (glyph runs, transformed ones) are counted, not kept.
struct DrawList {
    std::vector<Box> boxes;
    std::vector<Image> images;
    std::vector<Shadow> shadows;
    std::vector<DrawCommand> commands;
    std::vector<Clip> clips;
    /// The first a placeholder for none.
    std::vector<Gradient> gradients;
    std::uint32_t skipped = 0;
    float scale = 1;
};

/// A node of a tree, by its slot and generation; a node removed leaves its
/// handle stale.
struct Node {
    std::uint32_t index1 = 0;
    std::uint32_t generation = 0;
    friend constexpr bool operator==(Node, Node) noexcept = default;
};

class Tree {
public:
    /// A tree of at most `maximumNodes` nodes, their room reserved now.
    [[nodiscard]] static result::Result<std::unique_ptr<Tree>> create(std::uint32_t maximumNodes = 4096);

    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    ~Tree();

    /// A new root, `key` its owner's; refused past the limit.
    [[nodiscard]] result::Result<Node> add(std::uint64_t key);
    /// `child`, a root, becomes `parent`'s last child.
    [[nodiscard]] result::Status attach(Node parent, Node child);
    /// `node` becomes a root with its subtree; a root stays as it is.
    [[nodiscard]] result::Status detach(Node node);
    /// `node` and its subtree are gone.
    [[nodiscard]] result::Status remove(Node node);
    /// Whether `node` is still in the tree.
    [[nodiscard]] bool contains(Node node) const noexcept;
    /// The key `node` was added with; nought for one not in the tree.
    [[nodiscard]] std::uint64_t keyOf(Node node) const noexcept;

    /// `node`'s authored layout; refused for a value out of range (a
    /// negative gap or padding, `Auto` for its children's alignment).
    [[nodiscard]] result::Status setLayout(Node node, const Layout& layout);
    /// `root`'s subtree laid out in `width` by `height` pixels: a root's
    /// share is of that space, and an automatic root fits its content.
    [[nodiscard]] result::Status layOut(Node root, float width, float height);
    /// `node`'s rectangle from the last layout that reached it.
    [[nodiscard]] Rect rectOf(Node node) const noexcept;

    /// `node`'s look; refused for a negative radius.
    [[nodiscard]] result::Status setLook(Node node, const Look& look);
    /// What `root`'s subtree, laid out, draws, into `into`, its last
    /// contents replaced; `scale` device pixels a pixel, which edges snap to.
    [[nodiscard]] result::Status draw(Node root, float scale, DrawList& into);

    struct State;

private:
    explicit Tree(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::ui
