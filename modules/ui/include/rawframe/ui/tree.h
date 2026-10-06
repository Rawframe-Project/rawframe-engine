#pragma once

// A UI tree (ADR-0034, SPEC-0030, D374): Maul UI's retained core owned for
// the engine, its nodes found by a key the owner chooses (an entity's), laid
// out by SPEC-0030's Scale+Offset sizing and flexbox algebra into
// rectangles relative to their parents, and drawn as SPEC-0032's
// draw-command list. A node and a subtree that did not change are not laid
// out or painted again. A node may show text in a font the tree holds,
// measured by its lines and drawn as glyph runs (D384), each glyph's image
// rendered into the tree's glyph atlas (D398). Maul UI's types stay inside
// this module.

#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
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

/// A font a tree holds (D384), by the key Maul UI's text service gives it,
/// never nought; the null font, nought, names the tree's default.
struct Font {
    std::uint64_t key = 0;
    friend constexpr bool operator==(Font, Font) noexcept = default;
};

/// Where a node's lines sit across its content box: at the start, which
/// the text's direction sets, the center, or the end.
enum class TextAlign : std::uint8_t {
    Start,
    Center,
    End
};

/// How a node's text is shown (D384): its font, its size in pixels (an em),
/// its color 0xRRGGBBAA (sRGB, straight alpha), its line height times its
/// size (nought for the font's own), its weight (400 regular, 700 bold),
/// its lines' alignment, and whether they wrap to its width.
struct TextLook {
    Font font;
    float size = 16;
    std::uint32_t color = 0x000000FF;
    float lineHeight = 0;
    float weight = 400;
    TextAlign align = TextAlign::Start;
    bool wrap = true;
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

/// A glyph of a run (D384): its id in the run's font and its place from the
/// run's origin, in pixels, y down; and its image (D398): the rectangle it
/// is drawn in, from the root's top left in pixels, its edges on device
/// pixels, and where its coverage is in the list's atlas, in the atlas's
/// pixels. Both are empty for a glyph with no outline (a space), and for one
/// left out.
struct Glyph {
    std::uint32_t id = 0;
    float x = 0;
    float y = 0;
    Rect image;
    Rect atlas;
};

/// The coverage of the glyphs a tree draws (D398): `side` by `side` bytes,
/// rows from the top, each nought outside a glyph's outline to 255 inside,
/// linear in the area it covers; `revision` changes whenever they do.
/// Glyphs are rendered into it as lists first draw them, unhinted, at their
/// size in device pixels and the quarter of a device pixel their pen falls
/// on, and kept. It is Maul UI's glyph atlas, one page in plots a quarter of
/// its side (D404): when no plot has room, the one least recently drawn is
/// emptied, never one the list being drawn uses.
struct GlyphAtlas {
    std::uint32_t side = 0;
    std::vector<std::uint8_t> coverage;
    std::uint64_t revision = 0;
};

/// A run of glyphs to draw (D384): its font, its size in pixels (an em),
/// its color, linear with premultiplied alpha, its origin on the baseline,
/// its span of the list's glyphs, and its clip.
struct GlyphRun {
    Font font;
    float size = 0;
    std::array<float, 4> color{};
    float x = 0;
    float y = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint32_t clip = 0;
};

/// A command of a list in paint order: a box, an image, a shadow, or a run
/// of glyphs, by its index in the list's own.
struct DrawCommand {
    enum class Kind : std::uint8_t {
        Box,
        Image,
        Shadow,
        Glyphs
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

/// What a tree draws: its boxes, images, shadows, and glyph runs,
/// `commands` saying their paint order, and its clips, the first a
/// placeholder for none, in logical pixels, `scale` device pixels each.
/// Transformed commands, which generation 1 does not draw, are counted, not
/// kept.
struct DrawList {
    std::vector<Box> boxes;
    std::vector<Image> images;
    std::vector<Shadow> shadows;
    std::vector<GlyphRun> glyphRuns;
    std::vector<Glyph> glyphs;
    std::vector<DrawCommand> commands;
    std::vector<Clip> clips;
    /// The first a placeholder for none.
    std::vector<Gradient> gradients;
    std::uint32_t skipped = 0;
    float scale = 1;
    /// The tree's glyph atlas, which the glyphs' images are in (D398), until
    /// the tree draws again.
    const GlyphAtlas* atlas = nullptr;
    /// Glyphs with an outline left out: past what the atlas holds at once,
    /// or of a size or font that cannot be rendered.
    std::uint32_t glyphsLeftOut = 0;
};

/// A node of a tree, by its slot and generation; a node removed leaves its
/// handle stale.
struct Node {
    std::uint32_t index1 = 0;
    std::uint32_t generation = 0;
    friend constexpr bool operator==(Node, Node) noexcept = default;
};

/// How a node takes part in what points hit (D421): itself and its
/// children, its children alone (a container points pass through where it
/// has none), or neither with its subtree; whether input it is hit by and
/// leaves unused passes to what lies behind the UI, such as a game's world;
/// and whether it roots a layer, painted and hit above the content it is
/// in: an activation layer (a dialog, a menu), a modal one that points
/// missing it reach nothing below, or one in the overlay band above them
/// all (a popup, a tooltip).
struct Interaction {
    enum class Hits : std::uint8_t {
        Itself,
        Children,
        Nothing
    };
    enum class Layer : std::uint8_t {
        None,
        Activation,
        Modal,
        Overlay
    };
    Hits hits = Hits::Itself;
    bool passThrough = false;
    Layer layer = Layer::None;
};

/// What a point hits: the topmost node there, none for nothing, the point
/// in that node's box, and whether input passes through to what lies
/// behind the UI (always when nothing is hit, never when a modal layer
/// blocks it).
struct Hit {
    std::optional<Node> node;
    float x = 0;
    float y = 0;
    bool passThrough = true;
};

class Tree {
public:
    /// A tree of at most `maximumNodes` nodes and `maximumFonts` fonts,
    /// their room reserved now, and a glyph atlas `atlasSide` pixels square
    /// (D398), made when a glyph first needs it: from 64 to 16,384, a
    /// multiple of 4 (D404).
    [[nodiscard]] static result::Result<std::unique_ptr<Tree>>
    create(std::uint32_t maximumNodes = 4096, std::uint32_t maximumFonts = 64, std::uint32_t atlasSide = 1024);

    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    ~Tree();

    /// A new root, `key` its owner's; refused past the limit.
    [[nodiscard]] result::Result<Node> add(std::uint64_t key);
    /// `child`, a root, becomes `parent`'s last child.
    [[nodiscard]] result::Status attach(Node parent, Node child);
    /// `node` becomes a root with its subtree; a root stays as it is.
    [[nodiscard]] result::Status detach(Node node);
    /// `node` and its subtree are gone, with their text.
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
    /// How `node` takes part in what points hit; a node is hit in full and
    /// blocks until told otherwise.
    [[nodiscard]] result::Status setInteraction(Node node, const Interaction& interaction);
    /// What the point at `x`, `y` hits in `root`'s subtree as its last
    /// layout left it, the root at its own rectangle: layers from the top
    /// down, then the content they are not in, cut by every clip on the
    /// way, rounded corners included. Refused for a point not finite.
    [[nodiscard]] result::Result<Hit> hit(Node root, float x, float y) const;
    /// A font read from a TrueType or OpenType file's bytes, copied, or
    /// from `face` of a collection; refused for bytes that are not one,
    /// checked as hostile, and past the limit. The first becomes the
    /// default.
    [[nodiscard]] result::Result<Font> addFont(std::span<const std::byte> bytes, std::uint32_t face = 0);
    /// `font` is gone; text in it draws nothing, and in the default font,
    /// when it was that, nothing until another is made the default.
    [[nodiscard]] result::Status removeFont(Font font);
    /// The font the null font names.
    [[nodiscard]] result::Status setDefaultFont(Font font);
    /// `node` shows `text`, UTF-8 (ill-formed sequences as U+FFFD), in
    /// `look`, and is measured by its lines: broken where Unicode allows,
    /// ordered by the bidirectional algorithm, shaped by HarfBuzz. Its
    /// children inherit the look. Refused for a size that is not above
    /// nought, a weight outside 1 to 1000, or a negative line height.
    [[nodiscard]] result::Status setText(Node node, std::string_view text, const TextLook& look);
    /// `node` shows no text and has no size of its own again.
    [[nodiscard]] result::Status clearText(Node node);
    /// The times text could not be laid out for want of memory, and so
    /// measured as empty and drew nothing.
    [[nodiscard]] std::uint64_t textFailures() const noexcept;

    /// What `root`'s subtree, laid out, draws, into `into`, its last
    /// contents replaced; `scale` device pixels a pixel, which edges snap to.
    /// Its glyphs' images are rendered into the atlas as they are needed.
    [[nodiscard]] result::Status draw(Node root, float scale, DrawList& into);

    struct State;

private:
    explicit Tree(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::ui
