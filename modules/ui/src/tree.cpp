#include "rawframe/ui/tree.h"

#include "rawframe/ui/errors.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <maul-ui/context.h>
#include <maul-ui/draw.h>
#include <maul-ui/font.h>
#include <maul-ui/glyph_image.h>
#include <maul-ui/layout.h>
#include <maul-ui/node.h>
#include <maul-ui/style.h>
#include <maul-ui/text.h>
#include <maul-ui/text_block.h>
#include <maul-ui/text_style.h>
#include <maul-ui/visual.h>
#include <optional>
#include <string_view>
#include <unordered_map>

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
    case mui_errorFormat:
        return refuse(UiError::Format, why);
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

muiTextAlign textAlignOf(TextAlign align) noexcept {
    switch (align) {
    case TextAlign::Start:
        return mui_textAlignStart;
    case TextAlign::Center:
        return mui_textAlignCenter;
    case TextAlign::End:
        return mui_textAlignEnd;
    }
    return mui_textAlignStart;
}

/// A node's text: its block in the text service, and the node, whose
/// generation tells a node that took its slot after it was removed.
struct Text {
    muiNodeId node{};
    muiTextBlockId block{};
};

/// A glyph image as the atlas keeps it (D398): its font, glyph, em in
/// device pixels, and the quarter of a pixel its pen is past a pixel's edge.
struct GlyphKey {
    std::uint64_t font = 0;
    std::uint32_t glyph = 0;
    std::uint32_t size = 0;
    std::uint32_t quarter = 0;
    friend bool operator==(const GlyphKey&, const GlyphKey&) noexcept = default;
};

struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& key) const noexcept {
        std::uint64_t hash = key.font * 0x9E3779B97F4A7C15ULL;
        hash ^= (std::uint64_t{key.glyph} << 32U | key.size) + 0x632BE59BD9B4E019ULL + (hash << 6U) + (hash >> 2U);
        return static_cast<std::size_t>(hash ^ key.quarter);
    }
};

/// Where a glyph's coverage is in the atlas, and where its image goes from
/// its pen: its left edge right of it and its top edge above the baseline.
/// Empty for a glyph with no outline.
struct Placed {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int32_t left = 0;
    std::int32_t top = 0;
};

/// A row of the atlas glyphs are put in side by side, `x` the first pixel
/// free.
struct Shelf {
    std::uint32_t y = 0;
    std::uint32_t height = 0;
    std::uint32_t x = 0;
};

} // namespace

struct Tree::State {
    muiContext* context = nullptr;
    muiTextService* text = nullptr;
    muiTextHost host{};
    std::unordered_map<std::uint64_t, muiFontId> fonts;
    /// By the node's slot.
    std::unordered_map<std::uint32_t, Text> texts;
    GlyphAtlas atlas;
    std::unordered_map<GlyphKey, Placed, GlyphKeyHash> placed;
    std::vector<Shelf> shelves;
    /// The first row below every shelf.
    std::uint32_t shelvesEnd = 0;
    std::vector<unsigned char> rendered;
    /// Whether the atlas changed since its revision did.
    bool atlasChanged = false;

    ~State() {
        muiDestroyContext(context);
        muiDestroyTextService(text);
    }

    /// The text `node` shows; null for none.
    [[nodiscard]] const Text* textOf(muiNodeId node) const noexcept {
        const auto kFound = texts.find(node.index1);
        return kFound == texts.end() || kFound->second.node.generation != node.generation ? nullptr : &kFound->second;
    }

    /// Whether `node` is sized by its content, as text sizes it.
    [[nodiscard]] result::Status setContent(muiNodeId node, muiContentKind content) {
        muiLayoutStyle style{};
        RAWFRAME_TRY(checked(muiNode_GetLayoutStyle(context, node, &style), "a UI node's layout could not be read"));
        if (style.content != content) {
            style.content = content;
            RAWFRAME_TRY(checked(muiNode_SetLayoutStyle(context, node, &style), "a UI node's layout was refused"));
        }
        return checked(muiNode_MarkContentChanged(context, node), "a UI node's text could not be marked");
    }

    /// The atlas emptied, its glyphs rendered again as they are next drawn.
    void emptyAtlas() {
        placed.clear();
        shelves.clear();
        shelvesEnd = 0;
        std::ranges::fill(atlas.coverage, std::uint8_t{0});
        atlasChanged = true;
    }

    /// Room for `width` by `height` pixels, a pixel apart from the rest; none
    /// when the atlas is full.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> roomFor(std::uint32_t width, std::uint32_t height) {
        const std::uint32_t kWide = width + 1;
        const std::uint32_t kHigh = height + 1;
        // The lowest shelf that takes it without wasting much of its height.
        Shelf* best = nullptr;
        for (Shelf& shelf : shelves) {
            if (shelf.height >= kHigh && shelf.height <= kHigh + (kHigh / 4) + 2 && shelf.x + kWide <= atlas.side &&
                (best == nullptr || shelf.height < best->height)) {
                best = &shelf;
            }
        }
        if (best == nullptr) {
            if (shelvesEnd + kHigh > atlas.side || kWide > atlas.side) {
                return std::nullopt;
            }
            shelves.push_back(Shelf{.y = shelvesEnd, .height = kHigh, .x = 0});
            shelvesEnd += kHigh;
            best = &shelves.back();
        }
        const std::pair<std::uint32_t, std::uint32_t> kAt{best->x, best->y};
        best->x += kWide;
        return kAt;
    }

    /// `key`'s image in the atlas, rendered now if it is not there; none for
    /// one that cannot be rendered, and none with `full` set when the atlas
    /// has no room for it.
    std::optional<Placed> imageOf(const GlyphKey& key, float pixelSize, bool& full) {
        if (const auto kFound = placed.find(key); kFound != placed.end()) {
            return kFound->second;
        }
        muiGlyphImage image{};
        muiResult outcome = muiRenderGlyph(text,
                                           key.font,
                                           key.glyph,
                                           pixelSize,
                                           static_cast<float>(key.quarter) / 4.0F,
                                           &image,
                                           rendered.data(),
                                           rendered.size());
        const std::size_t kBytes = std::size_t{image.width} * image.height;
        if (outcome == mui_errorCapacity && kBytes > rendered.size()) {
            rendered.resize(kBytes);
            outcome = muiRenderGlyph(text,
                                     key.font,
                                     key.glyph,
                                     pixelSize,
                                     static_cast<float>(key.quarter) / 4.0F,
                                     &image,
                                     rendered.data(),
                                     rendered.size());
        }
        if (outcome != mui_success) {
            return std::nullopt;
        }
        Placed made{.width = image.width, .height = image.height, .left = image.left, .top = image.top};
        if (kBytes != 0) {
            if (atlas.coverage.empty()) {
                atlas.coverage.assign(std::size_t{atlas.side} * atlas.side, 0);
            }
            const auto kRoom = roomFor(image.width, image.height);
            if (!kRoom.has_value()) {
                full = true;
                return std::nullopt;
            }
            made.x = kRoom->first;
            made.y = kRoom->second;
            for (std::uint32_t row = 0; row < image.height; ++row) {
                std::memcpy(&atlas.coverage[((std::size_t{made.y} + row) * atlas.side) + made.x],
                            &rendered[std::size_t{row} * image.width],
                            image.width);
            }
            atlasChanged = true;
        }
        placed.emplace(key, made);
        return made;
    }

    /// Each glyph of `list` given its image, rendered into the atlas; when
    /// the atlas fills, it is emptied and the list's glyphs rendered again,
    /// and those that still do not fit are left out.
    void placeGlyphs(DrawList& list) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            bool full = false;
            list.glyphsLeftOut = 0;
            for (const GlyphRun& kRun : list.glyphRuns) {
                const float kPixelSize = kRun.size * list.scale;
                for (std::uint32_t at = kRun.first; at < kRun.first + kRun.count; ++at) {
                    Glyph& glyph = list.glyphs[at];
                    glyph.image = {};
                    glyph.atlas = {};
                    // The pen on a device pixel's edge and a quarter past it,
                    // the baseline on a pixel.
                    const float kPen = (kRun.x + glyph.x) * list.scale;
                    float pixel = std::floor(kPen);
                    auto quarter = static_cast<std::uint32_t>(std::lround((kPen - pixel) * 4.0F));
                    if (quarter == 4) {
                        pixel += 1;
                        quarter = 0;
                    }
                    const float kBaseline = std::round((kRun.y + glyph.y) * list.scale);
                    if (!(kPixelSize >= 1.0F / 64.0F && kPixelSize <= MUI_MAX_GLYPH_PIXEL_SIZE) ||
                        !std::isfinite(kPen) || !std::isfinite(kBaseline)) {
                        ++list.glyphsLeftOut;
                        continue;
                    }
                    const GlyphKey kKey{.font = kRun.font.key,
                                        .glyph = glyph.id,
                                        .size = std::bit_cast<std::uint32_t>(kPixelSize),
                                        .quarter = quarter};
                    const std::optional<Placed> kPlaced = imageOf(kKey, kPixelSize, full);
                    if (!kPlaced.has_value()) {
                        ++list.glyphsLeftOut;
                        continue;
                    }
                    if (kPlaced->width == 0 || kPlaced->height == 0) {
                        continue;
                    }
                    glyph.image = Rect{.x = (pixel + static_cast<float>(kPlaced->left)) / list.scale,
                                       .y = (kBaseline - static_cast<float>(kPlaced->top)) / list.scale,
                                       .width = static_cast<float>(kPlaced->width) / list.scale,
                                       .height = static_cast<float>(kPlaced->height) / list.scale};
                    glyph.atlas = Rect{.x = static_cast<float>(kPlaced->x),
                                       .y = static_cast<float>(kPlaced->y),
                                       .width = static_cast<float>(kPlaced->width),
                                       .height = static_cast<float>(kPlaced->height)};
                }
            }
            if (!full || attempt == 1) {
                break;
            }
            emptyAtlas();
        }
        if (atlasChanged) {
            ++atlas.revision;
            atlasChanged = false;
        }
        list.atlas = &atlas;
    }
};

namespace {

/// Maul UI's measure function: a node's text by its block, the host key
/// staying the owner's.
muiSize measureText(void* user, muiNodeId node, std::uint64_t, muiMeasureAxis width, muiMeasureAxis height) {
    auto* state = static_cast<Tree::State*>(user);
    const Text* kText = state->textOf(node);
    if (kText == nullptr) {
        return muiSize{.width = 0, .height = 0};
    }
    return muiMeasureText(&state->host, node, muiTextBlock_GetKey(kText->block), width, height);
}

void paintText(void* user, muiNodeId node, std::uint64_t, float width, float height, muiDrawSink* sink) {
    auto* state = static_cast<Tree::State*>(user);
    if (const Text* kText = state->textOf(node); kText != nullptr) {
        muiPaintText(&state->host, node, muiTextBlock_GetKey(kText->block), width, height, sink);
    }
}

} // namespace

Tree::Tree(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Tree::~Tree() = default;

result::Result<std::unique_ptr<Tree>>
Tree::create(std::uint32_t maximumNodes, std::uint32_t maximumFonts, std::uint32_t atlasSide) {
    muiContextDef def = muiDefaultContextDef();
    def.limits.nodes = maximumNodes;
    auto state = std::make_unique<State>();
    RAWFRAME_TRY(checked(muiCreateContext(&def, &state->context), "a UI tree could not be made"));
    muiTextServiceDef text = muiDefaultTextServiceDef();
    text.limits.fonts = maximumFonts;
    text.limits.textBlocks = maximumNodes;
    RAWFRAME_TRY(checked(muiCreateTextService(&text, &state->text), "a UI tree's text could not be made"));
    state->host = muiTextHost{.service = state->text, .context = state->context};
    state->atlas.side = atlasSide;
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
    RAWFRAME_TRY(checked(muiDestroyNode(state_->context, idOf(node)), "a UI node could not be removed"));
    // The subtree's text goes with it.
    std::erase_if(state_->texts, [this](const auto& entry) {
        if (muiNode_IsValid(state_->context, entry.second.node)) {
            return false;
        }
        (void)muiDestroyTextBlock(state_->text, entry.second.block);
        return true;
    });
    return {};
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
    if (state_->textOf(idOf(node)) != nullptr) {
        style.content = mui_contentHost;
    }
    return checked(muiNode_SetLayoutStyle(state_->context, idOf(node), &style), "a UI node's layout was refused");
}

result::Status Tree::layOut(Node root, float width, float height) {
    const muiLayoutInput kInput{.availableWidth = width,
                                .availableHeight = height,
                                .measure = measureText,
                                .measureUser = state_.get(),
                                .timeNs = 0};
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
    const auto kShadow = [](const ShadowLook& shadow) {
        return muiShadow{.color = colorOf(shadow.color),
                         .offsetX = shadow.x,
                         .offsetY = shadow.y,
                         .blur = shadow.blur,
                         .spread = shadow.spread};
    };
    style.outerShadow = kShadow(look.outerShadow);
    style.innerShadow = kShadow(look.innerShadow);
    if (look.gradient.kind != GradientLook::Kind::None) {
        style.gradient.kind = static_cast<muiGradientKind>(look.gradient.kind);
        style.gradient.angle = look.gradient.angle;
        style.gradient.stopCount = static_cast<std::uint8_t>(std::min<std::uint32_t>(look.gradient.stops, 255));
        for (std::size_t at = 0; at < look.gradient.colors.size(); ++at) {
            style.gradient.stops[at] =
                muiGradientStop{.color = colorOf(look.gradient.colors[at]), .position = look.gradient.positions[at]};
        }
    }
    return checked(muiNode_SetVisualValues(state_->context, idOf(node), &style, MUI_VISUAL_PROPERTIES),
                   "a UI node's look was refused");
}

result::Result<Font> Tree::addFont(std::span<const std::byte> bytes, std::uint32_t face) {
    muiFontDef def = muiDefaultFontDef();
    def.data = bytes.data();
    def.size = bytes.size();
    def.faceIndex = face;
    def.dataMode = mui_fontDataCopy;
    muiFontId made{};
    RAWFRAME_TRY(checked(muiCreateFont(state_->text, &def, &made), "a font could not be read"));
    if (state_->fonts.empty()) {
        RAWFRAME_TRY(checked(muiSetDefaultFont(state_->text, made), "a font could not be made the default"));
    }
    const Font kFont{.key = muiFont_GetKey(made)};
    state_->fonts.emplace(kFont.key, made);
    return kFont;
}

result::Status Tree::removeFont(Font font) {
    const auto kFound = state_->fonts.find(font.key);
    if (kFound == state_->fonts.end()) {
        return refuse(UiError::Stale, "a font not in the tree could not be removed");
    }
    RAWFRAME_TRY(checked(muiDestroyFont(state_->text, kFound->second), "a font could not be removed"));
    state_->fonts.erase(kFound);
    // Its glyphs' images go, and the default font's when it was that.
    state_->emptyAtlas();
    return {};
}

result::Status Tree::setDefaultFont(Font font) {
    const auto kFound = state_->fonts.find(font.key);
    if (kFound == state_->fonts.end()) {
        return refuse(UiError::Stale, "a font not in the tree could not be made the default");
    }
    RAWFRAME_TRY(checked(muiSetDefaultFont(state_->text, kFound->second), "a font could not be made the default"));
    // The null font's images were the old default's.
    state_->emptyAtlas();
    return {};
}

result::Status Tree::setText(Node node, std::string_view text, const TextLook& look) {
    if (!(std::isfinite(look.size) && look.size > 0) || !(look.weight >= 1 && look.weight <= 1000) ||
        !(std::isfinite(look.lineHeight) && look.lineHeight >= 0)) {
        return refuse(UiError::Invalid, "a node's text look is out of range");
    }
    if (look.font.key != 0 && !state_->fonts.contains(look.font.key)) {
        return refuse(UiError::Stale, "a node's text names a font not in the tree");
    }
    const muiNodeId kNode = idOf(node);
    if (!muiNode_IsValid(state_->context, kNode)) {
        return refuse(UiError::Stale, "text for a node not in the tree");
    }
    muiTextStyle values = muiDefaultTextStyle();
    values.color = colorOf(look.color);
    values.font = look.font.key;
    values.size = muiDimension{.scale = 0, .offset = look.size, .kind = mui_dimensionValue};
    if (look.lineHeight > 0) {
        values.lineHeight = muiDimension{.scale = look.lineHeight, .offset = 0, .kind = mui_dimensionValue};
    }
    values.weight = look.weight;
    values.align = textAlignOf(look.align);
    values.wrap = look.wrap ? mui_textWrap : mui_textNoWrap;
    RAWFRAME_TRY(checked(muiNode_SetTextValues(state_->context, kNode, &values, MUI_TEXT_PROPERTIES),
                         "a node's text look was refused"));
    if (const Text* kText = state_->textOf(kNode); kText != nullptr) {
        RAWFRAME_TRY(checked(muiTextBlock_SetText(state_->text, kText->block, text.data(), text.size()),
                             "a node's text could not be set"));
    } else {
        muiTextBlockId block{};
        RAWFRAME_TRY(checked(muiCreateTextBlock(state_->text, text.data(), text.size(), &block),
                             "a node's text could not be made"));
        // A slot's earlier node is gone, and its text with it.
        if (const auto kOld = state_->texts.find(kNode.index1); kOld != state_->texts.end()) {
            (void)muiDestroyTextBlock(state_->text, kOld->second.block);
        }
        state_->texts.insert_or_assign(kNode.index1, Text{.node = kNode, .block = block});
    }
    return state_->setContent(kNode, mui_contentHost);
}

result::Status Tree::clearText(Node node) {
    const muiNodeId kNode = idOf(node);
    const Text* kText = state_->textOf(kNode);
    if (kText == nullptr) {
        return {};
    }
    (void)muiDestroyTextBlock(state_->text, kText->block);
    state_->texts.erase(kNode.index1);
    return state_->setContent(kNode, mui_contentNone);
}

std::uint64_t Tree::textFailures() const noexcept {
    return muiGetTextServiceFailures(state_->text);
}

result::Status Tree::draw(Node root, float scale, DrawList& into) {
    const muiDrawInput kInput{.surface = 0, .scale = scale, .paint = paintText, .paintUser = state_.get()};
    RAWFRAME_TRY(checked(muiBuildDrawList(state_->context, idOf(root), &kInput), "a UI tree could not be drawn"));
    muiDrawList list{};
    RAWFRAME_TRY(checked(muiGetDrawList(state_->context, &list), "a UI tree's drawing could not be read"));
    into.boxes.clear();
    into.images.clear();
    into.shadows.clear();
    into.glyphRuns.clear();
    into.glyphs.clear();
    into.gradients.clear();
    into.commands.clear();
    into.clips.clear();
    into.skipped = 0;
    into.scale = scale;
    into.atlas = nullptr;
    into.glyphsLeftOut = 0;
    for (std::uint32_t at = 0; at < list.gradientCount; ++at) {
        const muiDrawGradient& kGradient = list.gradients[at];
        Gradient made{.kind = static_cast<GradientLook::Kind>(kGradient.kind),
                      .angle = kGradient.angle,
                      .stops = std::min<std::uint32_t>(kGradient.stopCount, MUI_MAX_DRAW_STOPS)};
        for (std::uint32_t stop = 0; stop < made.stops; ++stop) {
            made.colors.at(stop) = linearOf(kGradient.colors[stop]);
            made.positions.at(stop) = kGradient.positions[stop];
        }
        into.gradients.push_back(made);
    }
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
        if (kCommand.kind == mui_drawShadow && kCommand.transform == 0) {
            const muiDrawShadow& kShadow = kCommand.shadow;
            into.commands.push_back(DrawCommand{.kind = DrawCommand::Kind::Shadow,
                                                .index = static_cast<std::uint32_t>(into.shadows.size())});
            into.shadows.push_back(Shadow{.rect = ui::rectOf(kShadow.rect),
                                          .radii = cornersOf(kShadow.radii),
                                          .color = linearOf(kShadow.color),
                                          .x = kShadow.offsetX,
                                          .y = kShadow.offsetY,
                                          .blur = kShadow.blur,
                                          .spread = kShadow.spread,
                                          .inset = kShadow.inset != 0,
                                          .clip = kCommand.clip});
            continue;
        }
        if (kCommand.kind == mui_drawGlyphRun && kCommand.transform == 0) {
            const muiDrawGlyphRun& kRun = kCommand.glyphRun;
            if (kRun.firstGlyph > list.glyphCount || kRun.glyphCount > list.glyphCount - kRun.firstGlyph) {
                ++into.skipped;
                continue;
            }
            into.commands.push_back(DrawCommand{.kind = DrawCommand::Kind::Glyphs,
                                                .index = static_cast<std::uint32_t>(into.glyphRuns.size())});
            into.glyphRuns.push_back(GlyphRun{.font = Font{.key = kRun.font},
                                              .size = kRun.size,
                                              .color = linearOf(kRun.color),
                                              .x = kRun.originX,
                                              .y = kRun.originY,
                                              .first = static_cast<std::uint32_t>(into.glyphs.size()),
                                              .count = kRun.glyphCount,
                                              .clip = kCommand.clip});
            for (std::uint32_t glyph = 0; glyph < kRun.glyphCount; ++glyph) {
                const muiGlyph& kGlyph = list.glyphs[kRun.firstGlyph + glyph];
                into.glyphs.push_back(Glyph{.id = kGlyph.id, .x = kGlyph.x, .y = kGlyph.y});
            }
            continue;
        }
        if (kCommand.kind != mui_drawBox || kCommand.transform != 0) {
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
                                 .clip = kCommand.clip,
                                 .gradient = kBox.gradient < list.gradientCount ? kBox.gradient : 0});
    }
    state_->placeGlyphs(into);
    return {};
}

} // namespace rawframe::ui
