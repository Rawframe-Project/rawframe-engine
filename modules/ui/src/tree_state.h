#pragma once

// A UI tree's state and the helpers its sources share, private to this
// module: Maul UI's types stay inside it.

#include "rawframe/ui/errors.h"
#include "rawframe/ui/tree.h"

#include <cstring>
#include <maul-ui/context.h>
#include <maul-ui/glyph_atlas.h>
#include <maul-ui/layout.h>
#include <maul-ui/node.h>
#include <maul-ui/style.h>
#include <maul-ui/text.h>
#include <maul-ui/text_block.h>
#include <maul-ui/visual.h>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rawframe::ui {

inline std::unexpected<result::Error> refuse(UiError error, std::string_view why) {
    const result::ErrorClass kClass = error == UiError::Capacity ? result::ErrorClass::ResourceExhausted
                                      : error == UiError::Stale  ? result::ErrorClass::NotFound
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
    /// The atlas's coverage as the draw list hands it over: the one page
    /// of Maul UI's atlas, copied where it changed (D404).
    GlyphAtlas atlas;
    muiGlyphAtlas* glyphs = nullptr;
    std::vector<muiAtlasUpdate> updates;

    ~State() {
        muiDestroyGlyphAtlas(glyphs);
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

    /// Each glyph of `list` given its image from Maul UI's atlas, which
    /// renders and packs it the first time (D404). A draw is the atlas's
    /// frame: room is made by emptying the plot least recently drawn, never
    /// one this list uses, and a glyph is left out when there is none, or
    /// when it cannot be rendered or is larger than a plot.
    void placeGlyphs(DrawList& list) {
        list.glyphsLeftOut = 0;
        list.atlas = &atlas;
        if (list.glyphRuns.empty()) {
            return;
        }
        if (glyphs == nullptr) {
            muiGlyphAtlasDef def = muiDefaultGlyphAtlasDef();
            def.pageWidth = atlas.side;
            def.pageHeight = atlas.side;
            def.plotWidth = atlas.side / 4;
            def.plotHeight = atlas.side / 4;
            def.maxPages = 1;
            if (muiCreateGlyphAtlas(text, &def, &glyphs) != mui_success) {
                for (const GlyphRun& kRun : list.glyphRuns) {
                    list.glyphsLeftOut += kRun.count;
                }
                return;
            }
        }
        muiGlyphAtlas_NextFrame(glyphs);
        for (const GlyphRun& kRun : list.glyphRuns) {
            const float kPixelSize = kRun.size * list.scale;
            for (std::uint32_t at = kRun.first; at < kRun.first + kRun.count; ++at) {
                Glyph& glyph = list.glyphs[at];
                glyph.image = {};
                glyph.atlas = {};
                muiAtlasGlyph got{};
                if (muiGlyphAtlas_Get(glyphs,
                                      kRun.font.key,
                                      glyph.id,
                                      kPixelSize,
                                      (kRun.x + glyph.x) * list.scale,
                                      (kRun.y + glyph.y) * list.scale,
                                      &got) != mui_success) {
                    ++list.glyphsLeftOut;
                    continue;
                }
                if (got.width == 0 || got.height == 0) {
                    continue;
                }
                glyph.image = Rect{.x = static_cast<float>(got.x) / list.scale,
                                   .y = static_cast<float>(got.y) / list.scale,
                                   .width = static_cast<float>(got.width) / list.scale,
                                   .height = static_cast<float>(got.height) / list.scale};
                glyph.atlas = Rect{.x = static_cast<float>(got.u),
                                   .y = static_cast<float>(got.v),
                                   .width = static_cast<float>(got.width),
                                   .height = static_cast<float>(got.height)};
            }
        }
        takeUpdates();
    }

    /// The page's changed rectangles copied into the coverage the list
    /// hands over, its revision moved when there were any.
    void takeUpdates() {
        std::uint32_t count = 0;
        if (muiGlyphAtlas_TakeUpdates(glyphs, nullptr, 0, &count) == mui_errorCapacity) {
            updates.resize(count);
            if (muiGlyphAtlas_TakeUpdates(glyphs, updates.data(), count, &count) != mui_success) {
                return;
            }
        }
        muiAtlasPage page{};
        if (count == 0 || muiGlyphAtlas_GetPage(glyphs, 0, &page) != mui_success) {
            return;
        }
        if (atlas.coverage.empty()) {
            atlas.coverage.assign(std::size_t{atlas.side} * atlas.side, 0);
        }
        for (std::uint32_t at = 0; at < count; ++at) {
            const muiAtlasUpdate& kUpdate = updates[at];
            for (std::uint32_t row = kUpdate.y; row < kUpdate.y + kUpdate.height; ++row) {
                const std::size_t kAt = (std::size_t{row} * atlas.side) + kUpdate.x;
                std::memcpy(&atlas.coverage[kAt], &page.pixels[kAt], kUpdate.width);
            }
        }
        ++atlas.revision;
    }
};

} // namespace rawframe::ui
