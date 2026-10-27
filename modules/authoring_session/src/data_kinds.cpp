#include "data_kinds.h"

#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/audio/layout.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/blockout/blockout.h"
#include "rawframe/document/json.h"
#include "rawframe/input/actions.h"
#include "rawframe/localization/table.h"
#include "rawframe/scatter/scatter.h"
#include "rawframe/sequence/sequence.h"
#include "rawframe/terrain/terrain.h"
#include "rawframe/tilemap/tilemap.h"
#include "rawframe/world_kest/game.h"

#include <cmath>
#include <string>

namespace rawframe::authoring_session {

namespace {

/// What `read` makes of a document, or what its reader refused and
/// where, said to whoever asked as a document of the kind `called` that
/// does not read (`ValidationFailed`), as a style sheet's is.
template <class Read> result::Status readsAs(std::string_view called, Read read) {
    auto made = read();
    if (made.has_value()) {
        return {};
    }
    std::string why = "the " + std::string{called} + " does not read: " + std::string{made.error().description()};
    for (const result::ContextField& kField : made.error().context()) {
        if (kField.key == "path") {
            why += " at " + std::string{kField.value};
        }
    }
    return std::unexpected<result::Error>{
        std::move(made).error().mappedTo(result::ErrorClass::InvalidArgument,
                                         authoring::kAuthoringDomain,
                                         authoring::code(authoring::AuthoringError::ValidationFailed),
                                         why)};
}

result::Status readsActions(std::string_view text) {
    return readsAs("action map", [text] {
        return input::readActionSet(text);
    });
}
result::Status readsMixer(std::string_view text) {
    return readsAs("mixer", [text] {
        return audio::readLayout(text);
    });
}
result::Status readsStrings(std::string_view text) {
    return readsAs("string table", [text] {
        return localization::readStrings(text);
    });
}
result::Status readsTranslations(std::string_view text) {
    return readsAs("translation table", [text] {
        return localization::readTranslations(text);
    });
}
/// An animation document is a graph, a clip, or a skeleton, as its
/// kind says.
result::Status readsAnimation(std::string_view text) {
    const auto kParsed = document::parse(text);
    const document::Value* kKind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
    const std::string_view kNamed =
        kKind != nullptr && kKind->kind() == document::Value::Kind::String ? *kKind->text() : std::string_view{};
    if (kNamed == "animation.clip") {
        return readsAs("animation clip", [text] {
            return animation::readClip(text);
        });
    }
    if (kNamed == "animation.skeleton") {
        return readsAs("skeleton", [text] {
            return animation::readSkeleton(text);
        });
    }
    return readsAs("animation graph", [text] {
        return animation::readGraph(text);
    });
}

constexpr std::array<DataKind, 5> kKinds = {{{".actions", "action map", &readsActions},
                                             {".mixer", "mixer", &readsMixer},
                                             {".strings", "string table", &readsStrings},
                                             {".translations", "translation table", &readsTranslations},
                                             {".rfanim", "animation document", &readsAnimation}}};

/// A line document kind's refusal of an operation it cannot make lines of.
std::unexpected<result::Error> refusedLines(std::string_view why) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                       authoring::kAuthoringDomain,
                                                       authoring::code(authoring::AuthoringError::ValidationFailed),
                                                       why)};
}

/// A document written again, as its lines.
std::vector<std::string> linesOf(std::string_view written) {
    std::vector<std::string> made;
    std::size_t at = 0;
    while (at < written.size()) {
        const std::size_t kEnd = written.find('\n', at);
        made.emplace_back(written.substr(at, kEnd - at));
        at = kEnd + 1;
    }
    return made;
}

} // namespace

std::span<const DataKind> dataKinds() noexcept {
    return kKinds;
}

result::Status readsBlockout(std::string_view text) {
    return readsAs("blockout", [text]() -> result::Status {
        RAWFRAME_TRY_ASSIGN(const blockout::Blockout kRead, blockout::parseBlockout(text));
        RAWFRAME_TRY(blockout::meshOf(kRead));
        return {};
    });
}

result::Status readsTerrain(std::string_view text) {
    return readsAs("terrain", [text]() -> result::Status {
        RAWFRAME_TRY_ASSIGN(const terrain::Terrain kRead, terrain::parseTerrain(text));
        RAWFRAME_TRY(terrain::meshOf(kRead));
        return {};
    });
}

result::Result<std::vector<std::string>> rewritesTerrain(std::span<const std::string> lines,
                                                         const authoring::Operation& operation) {
    const auto* brush = std::get_if<authoring::BrushTerrain>(&operation);
    if (brush == nullptr) {
        return refusedLines("a terrain's operations are lines.set, lines.insert, and terrain.brush");
    }
    std::string text;
    for (const std::string& kLine : lines) {
        text += kLine + "\n";
    }
    auto read = terrain::parseTerrain(text);
    if (!read.has_value()) {
        return refusedLines("the terrain does not read: " + std::string{read.error().description()});
    }
    if (!(brush->radius > 0) || !std::isfinite(brush->x) || !std::isfinite(brush->z) || !std::isfinite(brush->amount) ||
        brush->radius > 1e4) {
        return refusedLines("a brush reaches a radius above nought, at a place of finite meters");
    }
    const terrain::Brush kBrush{.x = brush->x, .z = brush->z, .radius = brush->radius};
    if (brush->tool == "raise" || brush->tool == "lower") {
        static_cast<void>(terrain::raise(*read, kBrush, brush->tool == "raise" ? brush->amount : -brush->amount));
    } else if (brush->tool == "smooth") {
        static_cast<void>(terrain::smooth(*read, kBrush, brush->amount));
    } else if (brush->tool == "paint") {
        if (brush->amount < 0 || brush->amount != std::floor(brush->amount) ||
            brush->amount >= static_cast<double>(read->layers.size())) {
            return refusedLines("a paint's amount is the place of a layer the terrain names");
        }
        static_cast<void>(terrain::paint(*read, kBrush, static_cast<std::uint8_t>(brush->amount)));
    } else {
        return refusedLines("a brush's tool is raise, lower, smooth, or paint");
    }
    return linesOf(terrain::writeTerrain(*read));
}

result::Status readsScatter(std::string_view text) {
    return readsAs("scatter", [text]() -> result::Status {
        RAWFRAME_TRY(scatter::parseScatter(text));
        return {};
    });
}

result::Status readsSequence(std::string_view text) {
    return readsAs("sequence", [text]() -> result::Status {
        RAWFRAME_TRY(sequence::parseSequence(text));
        return {};
    });
}

result::Status readsTilemap(std::string_view text) {
    return readsAs("tilemap", [text]() -> result::Status {
        RAWFRAME_TRY(tilemap::parseTilemap(text));
        return {};
    });
}

result::Result<std::vector<std::string>> rewritesTilemap(std::span<const std::string> lines,
                                                         const authoring::Operation& operation) {
    const auto* paint = std::get_if<authoring::PaintTiles>(&operation);
    if (paint == nullptr) {
        return refusedLines("a tilemap's operations are lines.set, lines.insert, and tilemap.paint");
    }
    std::string text;
    for (const std::string& kLine : lines) {
        text += kLine + "\n";
    }
    auto read = tilemap::parseTilemap(text);
    if (!read.has_value()) {
        return refusedLines("the tilemap does not read: " + std::string{read.error().description()});
    }
    const auto kWhole = [](double value, double most) {
        return value >= 0 && value <= most && value == std::floor(value);
    };
    const double kTiles = static_cast<double>(read->sheetColumns) * read->sheetRows;
    if (!kWhole(paint->column, read->columns - 1.0) || !kWhole(paint->row, read->rows - 1.0) ||
        !kWhole(paint->width, read->columns - paint->column) || !kWhole(paint->height, read->rows - paint->row) ||
        paint->width < 1 || paint->height < 1 || !(paint->tile == -1 || kWhole(paint->tile, kTiles - 1))) {
        return refusedLines("tiles painted are a rectangle of whole cells within the grid, of a tile of its "
                            "sheet or -1 for none");
    }
    static_cast<void>(tilemap::paint(*read,
                                     static_cast<std::uint32_t>(paint->column),
                                     static_cast<std::uint32_t>(paint->row),
                                     static_cast<std::uint32_t>(paint->width),
                                     static_cast<std::uint32_t>(paint->height),
                                     paint->tile == -1 ? tilemap::kEmpty : static_cast<std::uint16_t>(paint->tile)));
    return linesOf(tilemap::writeTilemap(*read));
}

result::Status readsGameDescription(std::string_view text) {
    return readsAs("game description", [text] {
        return world_kest::parseGame(text);
    });
}

} // namespace rawframe::authoring_session
