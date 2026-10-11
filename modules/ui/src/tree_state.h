#pragma once

// A UI tree's state and the helpers its sources share, private to this
// module: Maul UI's types stay inside it.

#include "rawframe/ui/errors.h"
#include "rawframe/ui/tree.h"

#include <array>
#include <cstring>
#include <maul-ui/context.h>
#include <maul-ui/glyph_atlas.h>
#include <maul-ui/layout.h>
#include <maul-ui/node.h>
#include <maul-ui/style.h>
#include <maul-ui/text.h>
#include <maul-ui/text_block.h>
#include <maul-ui/transition.h>
#include <maul-ui/visual.h>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rawframe::ui {

inline std::unexpected<result::Error> refuse(UiError error, std::string_view why) {
    const result::ErrorClass kClass = error == UiError::Capacity      ? result::ErrorClass::ResourceExhausted
                                      : error == UiError::Stale       ? result::ErrorClass::NotFound
                                      : error == UiError::Unavailable ? result::ErrorClass::Unavailable
                                                                      : result::ErrorClass::InvalidArgument;
    return std::unexpected<result::Error>{result::fail(kClass, kUiDomain, code(error), why).error()};
}

/// Maul UI's answer as this module's.
inline result::Status checked(muiResult outcome, std::string_view why) {
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

inline muiNodeId idOf(Node node) noexcept {
    return muiNodeId{.index1 = node.index1, .generation = node.generation};
}

/// `look` as Maul UI's visual values, every part of it.
[[nodiscard]] muiVisualStyle visualOf(const Look& look) noexcept;

/// The visual properties `parts` name (D431).
[[nodiscard]] muiPropertyMask maskOf(LookParts parts) noexcept;

/// A node's text: its block in the text service, and the node, whose
/// generation tells a node that took its slot after it was removed.
struct Text {
    muiNodeId node{};
    muiTextBlockId block{};
    /// Made with the node, which Maul UI's editing finds by its key (D426):
    /// it lasts as long as the node.
    bool editable = false;
    /// How assistive technology reads an editable node (D613): its
    /// selection as its editor last told it, the caret at the second (none
    /// told: the caret at the end), and whether its text is a placeholder,
    /// read as none.
    std::optional<std::array<std::uint32_t, 2>> selection;
    bool placeholder = false;
};

/// The key an editable node's owner chose, which its host key, its block's,
/// is not; by the node, as `Text` is.
struct Owner {
    muiNodeId node{};
    std::uint64_t key = 0;
};

struct Tree::State {
    muiContext* context = nullptr;
    muiTextService* text = nullptr;
    muiTextHost host{};
    std::unordered_map<std::uint64_t, muiFontId> fonts;
    /// By the node's slot.
    std::unordered_map<std::uint32_t, Text> texts;
    /// By the editable node's slot.
    std::unordered_map<std::uint32_t, Owner> owners;
    /// The transition specs each style class made, by its key: gone with
    /// it, so a sheet made again does not fill the context's (D634).
    std::unordered_map<std::uint64_t, std::vector<muiTransitionId>> transitions;
    /// The atlas's coverage as the draw list hands it over: the one page
    /// of Maul UI's atlas, copied where it changed (D404).
    GlyphAtlas atlas;
    muiGlyphAtlas* glyphs = nullptr;
    std::vector<muiAtlasUpdate> updates;
    /// The field atlas as the draw list hands it over (D667), and Maul
    /// UI's of four channels it is copied from.
    FieldAtlas fields{.pixelSize = kFieldSize, .spread = kFieldSpread};
    muiGlyphAtlas* fieldGlyphs = nullptr;
    /// The color atlas likewise (D669).
    ColorAtlas colors;
    muiGlyphAtlas* colorGlyphs = nullptr;

    /// A field's em in its pixels, and how far it reaches past the outline
    /// (D667): about the size of large UI text, and four pixels, so a run
    /// a little smaller still has a pixel and more of field to smooth its
    /// edges.
    static constexpr float kFieldSize = 32;
    static constexpr std::uint32_t kFieldSpread = 4;

    ~State() {
        muiDestroyGlyphAtlas(colorGlyphs);
        muiDestroyGlyphAtlas(fieldGlyphs);
        muiDestroyGlyphAtlas(glyphs);
        muiDestroyContext(context);
        muiDestroyTextService(text);
    }

    /// The text `node` shows; null for none.
    [[nodiscard]] const Text* textOf(muiNodeId node) const noexcept {
        const auto kFound = texts.find(node.index1);
        return kFound == texts.end() || kFound->second.node.generation != node.generation ? nullptr : &kFound->second;
    }
    [[nodiscard]] Text* textOf(muiNodeId node) noexcept {
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

    /// Each glyph of `list` given its image from Maul UI's atlas, which
    /// renders and packs it the first time (D404). A draw is the atlas's
    /// frame: room is made by emptying the plot least recently drawn, never
    /// one this list uses, and a glyph is left out when there is none, or
    /// when it cannot be rendered or is larger than a plot.
    /// Each glyph of `list` given its image from Maul UI's atlases, which
    /// render and pack it the first time (D404): a run `kFieldsFrom` device
    /// pixels and larger its distance fields, scaled to its size (D667), a
    /// smaller one its coverage at its size; and a glyph its font colors,
    /// at any size, its color image at its size (D669). A draw is the
    /// atlases' frame:
    /// room is made by emptying the plot least recently drawn, never one
    /// this list uses, and a glyph is left out when there is none, or when
    /// it cannot be rendered or is larger than a plot.
    void placeGlyphs(DrawList& list) {
        list.glyphsLeftOut = 0;
        list.atlas = &atlas;
        list.fields = &fields;
        list.colors = &colors;
        if (list.glyphRuns.empty()) {
            return;
        }
        const bool kMade = made(glyphs, atlas.side, mui_atlasOneChannel) &&
                           made(fieldGlyphs, fields.side, mui_atlasFourChannel) &&
                           made(colorGlyphs, colors.side, mui_atlasColor);
        if (!kMade) {
            for (const GlyphRun& kRun : list.glyphRuns) {
                list.glyphsLeftOut += kRun.count;
            }
            return;
        }
        muiGlyphAtlas_NextFrame(glyphs);
        muiGlyphAtlas_NextFrame(fieldGlyphs);
        muiGlyphAtlas_NextFrame(colorGlyphs);
        for (GlyphRun& run : list.glyphRuns) {
            const float kPixelSize = run.size * list.scale;
            run.field = kPixelSize >= kFieldsFrom;
            // A field's pixels to the device's.
            const float kScaled = kPixelSize / kFieldSize;
            for (std::uint32_t at = run.first; at < run.first + run.count; ++at) {
                Glyph& glyph = list.glyphs[at];
                glyph.image = {};
                glyph.atlas = {};
                glyph.color = false;
                const float kPenX = (run.x + glyph.x) * list.scale;
                const float kPenY = (run.y + glyph.y) * list.scale;
                muiAtlasGlyph got{};
                // Its colors first: a font without them answers at once.
                const muiLinearColor kText{run.color[0], run.color[1], run.color[2], run.color[3]};
                const muiResult kColored = muiGlyphAtlas_GetColor(
                    colorGlyphs, run.font.key, glyph.id, kPixelSize, kPenX, kPenY, 0, kText, &got);
                if (kColored != mui_success && kColored != mui_empty) {
                    ++list.glyphsLeftOut;
                    continue;
                }
                if (kColored == mui_success) {
                    glyph.color = true;
                    if (got.width != 0 && got.height != 0) {
                        glyph.image = Rect{.x = static_cast<float>(got.x) / list.scale,
                                           .y = static_cast<float>(got.y) / list.scale,
                                           .width = static_cast<float>(got.width) / list.scale,
                                           .height = static_cast<float>(got.height) / list.scale};
                        glyph.atlas = Rect{.x = static_cast<float>(got.u),
                                           .y = static_cast<float>(got.v),
                                           .width = static_cast<float>(got.width),
                                           .height = static_cast<float>(got.height)};
                    }
                    continue;
                }
                const muiResult kGot =
                    run.field ? muiGlyphAtlas_GetMultiField(
                                    fieldGlyphs, run.font.key, glyph.id, kFieldSize, kFieldSpread, &got)
                              : muiGlyphAtlas_Get(glyphs, run.font.key, glyph.id, kPixelSize, kPenX, kPenY, &got);
                if (kGot != mui_success) {
                    ++list.glyphsLeftOut;
                    continue;
                }
                if (got.width == 0 || got.height == 0) {
                    continue;
                }
                const float kFrom = run.field ? kScaled : 1.0F;
                const float kX = run.field ? kPenX + (static_cast<float>(got.x) * kScaled) : static_cast<float>(got.x);
                const float kY = run.field ? kPenY + (static_cast<float>(got.y) * kScaled) : static_cast<float>(got.y);
                glyph.image = Rect{.x = kX / list.scale,
                                   .y = kY / list.scale,
                                   .width = static_cast<float>(got.width) * kFrom / list.scale,
                                   .height = static_cast<float>(got.height) * kFrom / list.scale};
                glyph.atlas = Rect{.x = static_cast<float>(got.u),
                                   .y = static_cast<float>(got.v),
                                   .width = static_cast<float>(got.width),
                                   .height = static_cast<float>(got.height)};
            }
        }
        takeUpdates(glyphs, atlas.side, 1, atlas.coverage, atlas.revision);
        takeUpdates(fieldGlyphs, fields.side, 4, fields.pixels, fields.revision);
        takeUpdates(colorGlyphs, colors.side, 4, colors.pixels, colors.revision);
    }

    /// `into`, an atlas of `side` and `format`, made if it is not yet.
    bool made(muiGlyphAtlas*& into, std::uint32_t side, muiAtlasFormat format) {
        if (into != nullptr) {
            return true;
        }
        muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
        def.pageWidth = side;
        def.pageHeight = side;
        def.plotWidth = side / 4;
        def.plotHeight = side / 4;
        def.maxPages = 1;
        def.format = format;
        return muiCreateGlyphAtlas(text, &def, &into) == mui_success;
    }

    /// The page's changed rectangles of `from`, `side` pixels of `bytes`
    /// each, copied into the pixels the list hands over, its revision moved
    /// when there were any.
    void takeUpdates(muiGlyphAtlas* from,
                     std::uint32_t side,
                     std::uint32_t bytes,
                     std::vector<std::uint8_t>& pixels,
                     std::uint64_t& revision) {
        std::uint32_t count = 0;
        if (muiGlyphAtlas_TakeUpdates(from, nullptr, 0, &count) == mui_errorCapacity) {
            updates.resize(count);
            if (muiGlyphAtlas_TakeUpdates(from, updates.data(), count, &count) != mui_success) {
                return;
            }
        }
        muiAtlasPage page{};
        if (count == 0 || muiGlyphAtlas_GetPage(from, 0, &page) != mui_success) {
            return;
        }
        if (pixels.empty()) {
            pixels.assign(std::size_t{side} * side * bytes, 0);
        }
        for (std::uint32_t at = 0; at < count; ++at) {
            const muiAtlasUpdate& kUpdate = updates[at];
            for (std::uint32_t row = kUpdate.y; row < kUpdate.y + kUpdate.height; ++row) {
                const std::size_t kAt = ((std::size_t{row} * side) + kUpdate.x) * bytes;
                std::memcpy(&pixels[kAt], &page.pixels[kAt], std::size_t{kUpdate.width} * bytes);
            }
        }
        ++revision;
    }
};

} // namespace rawframe::ui
