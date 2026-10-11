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
#include <cstddef>
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
/// The axes a node scrolls its children along, as Maul UI's scroll axes.
enum class Scroll : std::uint8_t {
    None,
    Horizontal,
    Vertical,
    Both
};

struct Layout {
    Dimension width;
    Dimension height;
    /// The least the node is, each automatic by default: as CSS's, the
    /// automatic minimum of a flex item is what its content needs, or
    /// nought for a scroll container, so a box holding scroll containers
    /// sets nought to let them, and not it, take what does not fit (D441).
    Dimension minWidth;
    Dimension minHeight;
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
    /// The axes the node scrolls its children along (D441): a node that
    /// scrolls is a scroll container, clipping its children at its rounded
    /// border box, and a wheel turned over it moves them.
    Scroll scroll = Scroll::None;
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

/// The parts of a look, as bits (D431): which a style class sets, or a node
/// sets for itself in place of its classes'. The image part is its key and
/// slice.
enum class LookPart : std::uint16_t {
    Fill = 1U << 0U,
    BorderColor = 1U << 1U,
    Radius = 1U << 2U,
    Clip = 1U << 3U,
    Image = 1U << 4U,
    ImageTint = 1U << 5U,
    OuterShadow = 1U << 6U,
    InnerShadow = 1U << 7U,
    Gradient = 1U << 8U
};

using LookParts = std::uint16_t;

inline constexpr LookParts kEveryLookPart = 0x1FF;

[[nodiscard]] constexpr LookParts operator|(LookPart left, LookPart right) noexcept {
    return static_cast<LookParts>(static_cast<LookParts>(left) | static_cast<LookParts>(right));
}

/// A style class a tree holds (SPEC-0030, D431): looks for a node's base and
/// its states, and how a part moves to a new value. Never nought.
struct Style {
    std::uint64_t key = 0;
    friend constexpr bool operator==(Style, Style) noexcept = default;
};

/// What a class sets a look for: a node in no state, or in one, SPEC-0030's
/// closed set. A later state's look wins over an earlier one's, as listed.
enum class Variant : std::uint8_t {
    Base,
    Focused,
    Hovered,
    Pressed,
    Disabled,
    /// A checkable node checked (D622); any other state's variant wins
    /// over it.
    Checked
};

/// The states a node is in, which pick its classes' variants.
struct States {
    bool focused = false;
    bool hovered = false;
    bool pressed = false;
    bool disabled = false;
    bool checked = false;
    friend constexpr bool operator==(const States&, const States&) noexcept = default;
};

/// How a part a class sets moves to a new value as a node's state or class
/// changes (SPEC-0030's two kinds): over `seconds` along an easing curve
/// after `delaySeconds`, the cubic Bezier's control points x1, y1, x2, y2
/// with both x from 0 to 1; or as a damped spring of `frequency` hertz and
/// `dampingRatio` (1 critical, below it overshoots), keeping its speed.
struct Transition {
    enum class Kind : std::uint8_t {
        Timed,
        Spring
    };
    enum class Easing : std::uint8_t {
        Linear,
        Ease,
        EaseIn,
        EaseOut,
        EaseInOut,
        CubicBezier
    };
    Kind kind = Kind::Timed;
    float seconds = 0.25F;
    float delaySeconds = 0;
    Easing easing = Easing::Ease;
    std::array<float, 4> bezier{};
    float frequency = 2;
    float dampingRatio = 1;
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
    friend constexpr bool operator==(Rect, Rect) noexcept = default;
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
    friend constexpr bool operator==(Box, Box) noexcept = default;
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
    friend constexpr bool operator==(Gradient, Gradient) noexcept = default;
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
    friend constexpr bool operator==(Image, Image) noexcept = default;
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
    friend constexpr bool operator==(Shadow, Shadow) noexcept = default;
};

/// A glyph of a run (D384): its id in the run's font and its place from the
/// run's origin, in pixels, y down; and its image (D398): the rectangle it
/// is drawn in, from the root's top left in pixels, its edges on device
/// pixels, and where its coverage is in the list's atlas, in the atlas's
/// pixels, or, in a run drawn from fields (D667), the rectangle its field
/// scales to and where its field is in the list's field atlas, or, for a
/// color glyph (D669), where its color image is in the list's color
/// atlas. Both are empty for a glyph with no outline (a space), and for
/// one left out.
struct Glyph {
    std::uint32_t id = 0;
    float x = 0;
    float y = 0;
    Rect image;
    Rect atlas;
    /// Drawn in its font's own colors (COLR and CPAL), from the color
    /// atlas, at any size of its run (D669).
    bool color = false;
    friend constexpr bool operator==(Glyph, Glyph) noexcept = default;
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

/// The distance fields of the glyphs a tree draws large (ADR-0049's MSDF
/// primary path, D667): `side` by `side` pixels of four bytes, rows from
/// the top, each glyph's multi-channel signed distance field rendered once
/// at `pixelSize` and drawn at any larger or somewhat smaller size. Red,
/// green, and blue are three distances whose median is the outline's, with
/// its corners kept sharp; alpha is the one-channel field. Each is 128 at
/// the outline and 128 / `spread` more for each of the field's pixels
/// inside, less outside. Maul UI's glyph atlas of four channels, as
/// `GlyphAtlas` is of one; `revision` changes whenever its pixels do.
struct FieldAtlas {
    std::uint32_t side = 0;
    std::vector<std::uint8_t> pixels;
    std::uint64_t revision = 0;
    float pixelSize = 0;
    std::uint32_t spread = 0;
};

/// The color glyphs a tree draws (ADR-0049's color glyphs on the bitmap
/// path, D669): `side` by `side` pixels of four bytes, rows from the top,
/// red, green, and blue sRGB-encoded and premultiplied, alpha linear, as an
/// sRGB texture holds them; each glyph rendered from its font's COLR
/// layers or paint graph and CPAL palette, the text's color where the font
/// names it, at its size in device pixels, as coverage is. Maul UI's glyph
/// atlas of color; `revision` changes whenever its pixels do.
struct ColorAtlas {
    std::uint32_t side = 0;
    std::vector<std::uint8_t> pixels;
    std::uint64_t revision = 0;
};

/// A run whose size is at least this many device pixels is drawn from
/// distance fields (D667); a smaller one from coverage rendered at its own
/// size, sharper there.
inline constexpr float kFieldsFrom = 24;

/// A run of glyphs to draw (D384): its font, its size in pixels (an em),
/// its color, linear with premultiplied alpha, its origin on the baseline,
/// its span of the list's glyphs, and its clip; and whether its glyphs are
/// drawn from the field atlas (D667), as runs `kFieldsFrom` device pixels
/// and larger are.
struct GlyphRun {
    Font font;
    float size = 0;
    std::array<float, 4> color{};
    float x = 0;
    float y = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint32_t clip = 0;
    bool field = false;
    friend constexpr bool operator==(GlyphRun, GlyphRun) noexcept = default;
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
    friend constexpr bool operator==(DrawCommand, DrawCommand) noexcept = default;
};

/// A clip: drawing kept inside the rounded rectangle (outside it, when
/// inverted) and inside its parent, an index of the list's clips.
struct Clip {
    Rect rect;
    std::array<float, 4> radii{};
    std::uint32_t parent = 0;
    bool invert = false;
    friend constexpr bool operator==(Clip, Clip) noexcept = default;
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
    /// The tree's field atlas, which the fields of the runs drawn from
    /// fields are in (D667), until the tree draws again.
    const FieldAtlas* fields = nullptr;
    /// The tree's color atlas, which color glyphs' images are in (D669),
    /// until the tree draws again.
    const ColorAtlas* colors = nullptr;
    /// Glyphs with an outline left out: past what the atlases hold at once,
    /// or of a size or font that cannot be rendered.
    std::uint32_t glyphsLeftOut = 0;
    /// The same commands of the same things, the atlases by address:
    /// whether their glyphs changed is their `revision`s' to say.
    friend bool operator==(const DrawList&, const DrawList&) = default;
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
    /// Whether the node may hold focus, as its owner's navigation gives it
    /// (D573): told to assistive technology with `focus`.
    bool focusable = false;
    /// Whether a press on it becomes a drag once the pointer moves past
    /// the drag threshold, as a range's does (D621).
    bool drags = false;
};

/// A node's value between a minimum and a maximum (SPEC-0030's
/// `UiRangeValue` and `UiDragValue`, D621), as a slider holds one: kept
/// within them and on a step from the minimum (none for any value), moved
/// a page by a press on the track and with the pointer by a drag, which
/// keeps where `thumb` (a node inside it, none for the value to follow
/// the pointer) was grabbed. The minimum is at the start of a horizontal
/// range and at the bottom of a vertical one. Maul UI's ranges.
struct Range {
    float minimum = 0;
    float maximum = 100;
    float value = 0;
    float step = 1;
    float page = 10;
    bool vertical = false;
    std::optional<Node> thumb;
};

/// Where a node goes beside another, its anchor (SPEC-0030's `UiPopup`,
/// D623): below, above, at its start or end, or on its center, centered
/// along that side, `gap` pixels from it, then flipped to the other side
/// where it would overflow the root and that side has more room, and kept
/// `margin` pixels inside the root. Placed after each layout, wherever the
/// node is in the tree; it takes no room there. Maul UI's popups, which
/// the owner opens and closes.
struct Popup {
    enum class Side : std::uint8_t {
        Below,
        Above,
        Start,
        End,
        Center
    };
    Node anchor;
    Side side = Side::Below;
    float gap = 0;
    float margin = 0;
};

/// A pointer's event for `Tree::point` (D621): the primary button pressed
/// or released, a move, the pointer cancelled, or gone from the window,
/// at a point in the root's space; whether the button is held after it.
struct PointerEvent {
    enum class Action : std::uint8_t {
        Move,
        Press,
        Release,
        Cancel,
        Leave
    };
    Action action = Action::Move;
    float x = 0;
    float y = 0;
    bool held = false;
    double seconds = 0;
};

/// What a node is to assistive technology (SPEC-0030's role vocabulary,
/// D571): its owner says, from what the node does. A node told nothing
/// only lays out others, and a screen reader shows its children in its
/// place.
enum class Role : std::uint8_t {
    Generic,
    /// Text that names or tells.
    Label,
    Image,
    Button,
    /// A field of one line, and of many.
    TextInput,
    MultilineTextInput,
    ScrollView,
    /// A layer that holds the input while it is up, as a modal one does.
    Dialog,
    /// Nodes that belong together, named as one.
    Group,
    /// A range the user moves (D621).
    Slider,
    /// Words beside a node that tell about it (D623).
    Tooltip,
    /// What the user checks and unchecks: a box, an on and off switch, or
    /// one of a group of which one is checked (D622). Its checked state is
    /// read from its states.
    CheckBox,
    Switch,
    RadioButton
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

/// A place between grapheme clusters of a node's text (D426): a byte
/// offset into its UTF-8 text, and whether it keeps to the text before it
/// where the offset has two places (a wrapped line's end or the next one's
/// start, either side of a change of direction).
struct TextPosition {
    std::uint32_t offset = 0;
    bool upstream = false;
    friend constexpr bool operator==(TextPosition, TextPosition) noexcept = default;
};

/// Where a position moves to in a node's text as it is drawn: by grapheme
/// cluster in the text's order or on screen, by word (UAX #29 segments with
/// a letter or a number), to its line's ends, a line up or down, or to the
/// text's ends.
enum class TextMove : std::uint8_t {
    NextCluster,
    PreviousCluster,
    Left,
    Right,
    NextWordStart,
    NextWordEnd,
    PreviousWordStart,
    LineStart,
    LineEnd,
    LineUp,
    LineDown,
    TextStart,
    TextEnd
};

/// A styled part of an input method's composition, in bytes of its text:
/// plain, still to convert (a thin underline), the part a conversion works
/// on now (a thick one), or converted and not yet committed (a thin one).
struct CompositionPart {
    enum class Style : std::uint8_t {
        Plain,
        Underline,
        Target,
        Converted
    };
    std::uint32_t start = 0;
    std::uint32_t length = 0;
    Style style = Style::Underline;
};

/// The bytes of a node's text a range covers, `end` the byte after it.
struct TextRange {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    friend constexpr bool operator==(TextRange, TextRange) noexcept = default;
};

/// Composition parts one composition has at most.
inline constexpr std::size_t kMaximumCompositionParts = 32;

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
    /// The same, its text editable (D426): only such a node's is, since
    /// Maul UI's editing finds a node's text by a key the node is made
    /// with. It shows its text once it is set.
    [[nodiscard]] result::Result<Node> addEditable(std::uint64_t key);
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
    /// Classes' transitions move to `seconds`, a monotonic clock's time; an
    /// earlier time than the last counts as none passed.
    [[nodiscard]] result::Status layOut(Node root, float width, float height, double seconds = 0);
    /// `node`'s rectangle from the last layout that reached it.
    [[nodiscard]] Rect rectOf(Node node) const noexcept;

    /// `node`'s look; refused for a negative radius. Only its `parts` are
    /// the node's own: the others come from its classes (D431).
    [[nodiscard]] result::Status setLook(Node node, const Look& look, LookParts parts = kEveryLookPart);

    /// A new style class, setting nothing.
    [[nodiscard]] result::Result<Style> addStyle();
    [[nodiscard]] result::Status removeStyle(Style style);
    /// The `parts` of `look` `style` gives a node in `variant`; the others
    /// it leaves as they were. Refused as setLook refuses a look.
    [[nodiscard]] result::Status setStyleLook(Style style, Variant variant, const Look& look, LookParts parts);
    /// How `parts` move as a node comes into `variant` of `style`; refused
    /// for a time below nought, a spring of no frequency or damping, or
    /// control points' x outside nought to one.
    [[nodiscard]] result::Status
    setStyleTransition(Style style, Variant variant, LookParts parts, const Transition& transition);
    /// `node`'s classes, the later winning where two set a part; at most 8.
    [[nodiscard]] result::Status setClasses(Node node, std::span<const Style> styles);
    [[nodiscard]] result::Status setStates(Node node, States states);
    /// Whether a part of `node` is moving to a new value, for a host that
    /// draws only on change.
    [[nodiscard]] bool transitioning(Node node) const noexcept;
    /// How `node` takes part in what points hit; a node is hit in full and
    /// blocks until told otherwise.
    [[nodiscard]] result::Status setInteraction(Node node, const Interaction& interaction);
    /// What `node` is to assistive technology and the name it is read by,
    /// none for a node its own text or children name (D571).
    [[nodiscard]] result::Status setRole(Node node, Role role);
    [[nodiscard]] result::Status setName(Node node, std::string_view name);
    /// The node holding focus as its owner's navigation or a press gave it,
    /// none for none (D573): what a screen reader follows. Refused for a
    /// node that is not focusable.
    [[nodiscard]] result::Status focus(std::optional<Node> node, bool navigated);
    /// What the point at `x`, `y` hits in `root`'s subtree as its last
    /// layout left it, the root at its own rectangle: layers from the top
    /// down, then the content they are not in, cut by every clip on the
    /// way, rounded corners included. Refused for a point not finite.
    [[nodiscard]] result::Result<Hit> hit(Node root, float x, float y) const;
    /// A wheel turned at `x`, `y` (as `hit` takes points) by `deltaX`,
    /// `deltaY` detents, positive y away from the user as the window gives
    /// them, `seconds` a monotonic clock's time (D441): the scroll
    /// container under the point that can move that way scrolls a step,
    /// eased over the next layouts, the one it scrolled last while turns
    /// keep coming; whether a container took it. Refused for a point or a
    /// turn not finite.
    [[nodiscard]] result::Result<bool> wheel(Node root, float x, float y, float deltaX, float deltaY, double seconds);
    /// `node` scrolled to `x`, `y` at once, kept within what its children
    /// reach as the last layout measured it.
    [[nodiscard]] result::Status scrollTo(Node node, float x, float y);
    /// `node`'s scroll offset; nought along an axis it does not scroll.
    [[nodiscard]] std::array<float, 2> scrollOf(Node node) const noexcept;
    /// `node` made a range, or its range set anew, its value kept within
    /// it (D621); refused for numbers not finite, a maximum below the
    /// minimum, a negative step or page, or a thumb not in the tree.
    [[nodiscard]] result::Status setRange(Node node, const Range& range);
    /// `node` no longer a range, whether or not it was one.
    [[nodiscard]] result::Status clearRange(Node node);
    /// `node`'s range with the value it holds now, which presses and drags
    /// move; none for a node that is not a range.
    [[nodiscard]] std::optional<Range> rangeOf(Node node) const;
    /// `node`'s range moved a step up or down, as an arrow key moves it: a
    /// hundredth of its span for one of any value. Whether its value moved;
    /// false for a node that is not a range, or one at its end.
    [[nodiscard]] bool stepRange(Node node, bool up);
    /// `node` placed beside an anchor from the next layout on (D623);
    /// refused for an anchor not in the tree, a gap or margin negative or
    /// not finite, or past the tree's popups.
    [[nodiscard]] result::Status setPopup(Node node, const Popup& popup);
    /// `node` no longer placed beside another, whether or not it was.
    [[nodiscard]] result::Status clearPopup(Node node);
    /// The pointer's event in `root`'s space (D621), for the ranges under
    /// it: a press on a range pages its value toward the point, and a drag
    /// moves it. Whether a range took it. Only a press the owner gives a
    /// range, and the moves and release after it, need come here: what
    /// hovers and presses otherwise is the owner's `hit` and `setStates`.
    [[nodiscard]] result::Result<bool> point(Node root, const PointerEvent& event);
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

    // Editing a node's text (D426), as it was last laid out; positions and
    // rectangles in the node's border box, pixels from its top left. Each
    // is refused for a node not added editable.

    /// The text `node` shows, valid until it changes; empty for none.
    [[nodiscard]] std::string_view textOf(Node node) const noexcept;
    /// The position nearest the point: on the line at its y, at the edge of
    /// the grapheme cluster nearer its x. Refused for a point not finite.
    [[nodiscard]] result::Result<TextPosition> textAt(Node node, float x, float y) const;
    /// Where the caret of `position` is drawn: a rectangle no wider than
    /// nought, its line's top and height.
    [[nodiscard]] result::Result<Rect> caretOf(Node node, TextPosition position) const;
    /// `from` moved through the text; one that cannot move stays. Moving a
    /// line up or down keeps `preferredX`, NaN for `from`'s own caret's.
    [[nodiscard]] result::Result<TextPosition>
    moved(Node node, TextPosition from, TextMove move, float preferredX) const;
    /// The rectangles `range` covers, a line's side by side clusters as
    /// one, lines from the top.
    [[nodiscard]] result::Result<std::vector<Rect>> rangeRects(Node node, TextRange range) const;
    /// What deleting from `offset` removes: back one code point as
    /// Backspace does (a cluster with an emoji, a regional indicator, or a
    /// keycap whole, a CR with its LF), or forward the next cluster.
    [[nodiscard]] result::Result<TextRange> deletion(Node node, std::uint32_t offset, bool forward) const;
    /// `range` of the text replaced by `text`, UTF-8; a composition before
    /// or after it moves, and one it overlaps ends. Refused for a range
    /// past the text.
    [[nodiscard]] result::Status replaceText(Node node, TextRange range, std::string_view text);
    /// Where an editable node's text is selected, the caret at `focus`, bytes
    /// into it, as assistive technology reads it (D613): its editor tells
    /// it as the selection moves, so a screen reader may type into the
    /// field where its user is.
    [[nodiscard]] result::Status setSelection(Node node, std::uint32_t anchor, std::uint32_t focus);
    /// Whether an editable node's text is a placeholder shown in place of
    /// none, which assistive technology reads as no text (D613).
    [[nodiscard]] result::Status setPlaceholder(Node node, bool placeholder);
    /// The input method's composition: `text` replaces the one there is, or
    /// goes in at `offset` when there is none, drawn underlined by `parts`
    /// (all of it thinly when there are none); empty text removes it.
    /// Refused for parts past the text or past kMaximumCompositionParts.
    [[nodiscard]] result::Status
    setComposition(Node node, std::uint32_t offset, std::string_view text, std::span<const CompositionPart> parts);
    /// The composition ends, its text kept as typed.
    [[nodiscard]] result::Status endComposition(Node node);
    /// Where the composition is; the empty range at nought for none.
    [[nodiscard]] result::Result<TextRange> composition(Node node) const;
    /// `node`'s border box where the last layout placed it, in the space
    /// points hit `root` in (the root at its own rectangle); refused for a
    /// node not in `root`'s subtree.
    [[nodiscard]] result::Result<Rect> placeOf(Node root, Node node) const;

    /// What `root`'s subtree, laid out, draws, into `into`, its last
    /// contents replaced; `scale` device pixels a pixel, which edges snap to.
    /// Its glyphs' images are rendered into the atlas as they are needed.
    [[nodiscard]] result::Status draw(Node root, float scale, DrawList& into);

    struct State;

private:
    /// A tree's accessibility reads the context it holds (access.h).
    friend class Access;
    explicit Tree(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::ui
