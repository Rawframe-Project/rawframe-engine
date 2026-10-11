#include "rawframe/ui/document.h"

#include "rawframe/base/hex64.h"
#include "rawframe/document/json.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <set>
#include <span>
#include <string>

namespace rawframe::ui {

namespace {

using document::invalid;
using document::Record;
using document::Value;

/// How a field of `rawframe.ui.Node` is written.
enum class Form : std::uint8_t {
    /// A number of logical pixels, a share, degrees, or an em, within
    /// +/-4096 (an f32).
    Real,
    /// `#rrggbbaa` (a u32 0xRRGGBBAA).
    Color,
    /// 16 lowercase hexadecimal digits (a u64).
    Identity,
    /// `true` (a u32 of one).
    Truth,
    /// An integer of an i32, or of an i64.
    Whole32,
    Whole64,
    /// A text field's most bytes, at most 252 (a u32).
    Bytes,
    /// A case of a closed set by name (a u32, its place).
    Case,
};

struct FieldForm {
    std::string_view name;
    std::size_t offset = 0;
    Form form = Form::Real;
    std::span<const std::string_view> cases;
};

constexpr std::array<std::string_view, 4> kDirections = {"row", "row_reverse", "column", "column_reverse"};
constexpr std::array<std::string_view, 6> kJustifications = {
    "start", "end", "center", "space_between", "space_around", "space_evenly"};
constexpr std::array<std::string_view, 5> kAlignments = {"auto", "stretch", "start", "end", "center"};
constexpr std::array<std::string_view, 3> kGradients = {"none", "linear", "radial"};
constexpr std::array<std::string_view, 3> kTextAligns = {"start", "center", "end"};
constexpr std::array<std::string_view, 2> kWraps = {"wrap", "no_wrap"};
constexpr std::array<std::string_view, 3> kHits = {"children", "self", "none"};
constexpr std::array<std::string_view, 4> kLayers = {"none", "activation", "modal", "overlay"};
constexpr std::array<std::string_view, 4> kEdits = {"none", "line", "message", "lines"};
constexpr std::array<std::string_view, 4> kScrolls = {"none", "horizontal", "vertical", "both"};

// rawframe.ui.Node's fields in its order.
constexpr std::array<FieldForm, 51> kFieldForms = {{
    {"widthScale", offsetof(NodeFields, widthScale), Form::Real, {}},
    {"widthOffset", offsetof(NodeFields, widthOffset), Form::Real, {}},
    {"heightScale", offsetof(NodeFields, heightScale), Form::Real, {}},
    {"heightOffset", offsetof(NodeFields, heightOffset), Form::Real, {}},
    {"direction", offsetof(NodeFields, direction), Form::Case, kDirections},
    {"justify", offsetof(NodeFields, justify), Form::Case, kJustifications},
    {"alignItems", offsetof(NodeFields, alignItems), Form::Case, kAlignments},
    {"alignSelf", offsetof(NodeFields, alignSelf), Form::Case, kAlignments},
    {"gap", offsetof(NodeFields, gap), Form::Real, {}},
    {"grow", offsetof(NodeFields, grow), Form::Real, {}},
    {"shrink", offsetof(NodeFields, shrink), Form::Real, {}},
    {"padding", offsetof(NodeFields, padding), Form::Real, {}},
    {"margin", offsetof(NodeFields, margin), Form::Real, {}},
    {"border", offsetof(NodeFields, border), Form::Real, {}},
    {"absolute", offsetof(NodeFields, absolute), Form::Truth, {}},
    {"xScale", offsetof(NodeFields, xScale), Form::Real, {}},
    {"xOffset", offsetof(NodeFields, xOffset), Form::Real, {}},
    {"yScale", offsetof(NodeFields, yScale), Form::Real, {}},
    {"yOffset", offsetof(NodeFields, yOffset), Form::Real, {}},
    {"anchorX", offsetof(NodeFields, anchorX), Form::Real, {}},
    {"anchorY", offsetof(NodeFields, anchorY), Form::Real, {}},
    {"fill", offsetof(NodeFields, fill), Form::Color, {}},
    {"borderColor", offsetof(NodeFields, borderColor), Form::Color, {}},
    {"radius", offsetof(NodeFields, radius), Form::Real, {}},
    {"clip", offsetof(NodeFields, clip), Form::Truth, {}},
    {"order", offsetof(NodeFields, order), Form::Whole32, {}},
    {"image", offsetof(NodeFields, image), Form::Identity, {}},
    {"imageTint", offsetof(NodeFields, imageTint), Form::Color, {}},
    {"imageSlice", offsetof(NodeFields, imageSlice), Form::Real, {}},
    {"shadowColor", offsetof(NodeFields, shadowColor), Form::Color, {}},
    {"shadowX", offsetof(NodeFields, shadowX), Form::Real, {}},
    {"shadowY", offsetof(NodeFields, shadowY), Form::Real, {}},
    {"shadowBlur", offsetof(NodeFields, shadowBlur), Form::Real, {}},
    {"gradientKind", offsetof(NodeFields, gradientKind), Form::Case, kGradients},
    {"gradientAngle", offsetof(NodeFields, gradientAngle), Form::Real, {}},
    {"gradientFrom", offsetof(NodeFields, gradientFrom), Form::Color, {}},
    {"gradientTo", offsetof(NodeFields, gradientTo), Form::Color, {}},
    {"text", offsetof(NodeFields, text), Form::Identity, {}},
    {"textValue", offsetof(NodeFields, textValue), Form::Whole64, {}},
    {"font", offsetof(NodeFields, font), Form::Identity, {}},
    {"textSize", offsetof(NodeFields, textSize), Form::Real, {}},
    {"textColor", offsetof(NodeFields, textColor), Form::Color, {}},
    {"textAlign", offsetof(NodeFields, textAlign), Form::Case, kTextAligns},
    {"textWrap", offsetof(NodeFields, textWrap), Form::Case, kWraps},
    {"press", offsetof(NodeFields, press), Form::Whole64, {}},
    {"hit", offsetof(NodeFields, hit), Form::Case, kHits},
    {"layer", offsetof(NodeFields, layer), Form::Case, kLayers},
    {"edit", offsetof(NodeFields, edit), Form::Case, kEdits},
    {"editLimit", offsetof(NodeFields, editLimit), Form::Bytes, {}},
    {"style", offsetof(NodeFields, style), Form::Identity, {}},
    {"scroll", offsetof(NodeFields, scroll), Form::Case, kScrolls},
}};

/// A node's members in the order it writes them: its component, its
/// words, Node's fields, its children.
constexpr std::array<std::string_view, kFieldForms.size() + 3> kNodeMembers = [] {
    std::array<std::string_view, kFieldForms.size() + 3> members{};
    members[0] = "component";
    members[1] = "shows";
    for (std::size_t index = 0; index < kFieldForms.size(); ++index) {
        members[index + 2] = kFieldForms[index].name;
    }
    members.back() = "children";
    return members;
}();
constexpr std::array<std::string_view, 3> kDocumentFields = {"kind", "formatVersion", "nodes"};
constexpr double kMostMagnitude = 4096;
constexpr std::int64_t kMostEditBytes = 252;

std::string indexed(const std::string& path, std::size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

template <typename T> void store(NodeFields& fields, std::size_t offset, T value) noexcept {
    std::memcpy(reinterpret_cast<std::byte*>(&fields) + offset, &value, sizeof value);
}

/// 16 lowercase hexadecimal digits, or `#` and 8 of them; nought is the
/// default, so not written.
result::Result<std::uint64_t> hexOf(const Record& record, std::string_view field, std::string_view text, bool color) {
    const std::optional<std::uint64_t> kValue =
        color ? (text.size() == 9 && text.front() == '#'
                     ? base::parseHex64(std::string(8, '0') + std::string{text.substr(1)})
                     : std::nullopt)
              : base::parseHex64(text);
    if (!kValue.has_value()) {
        return invalid(record.pathOf(field),
                       color ? "a color is # and 8 lowercase hexadecimal digits, #rrggbbaa"
                             : "an identity is 16 lowercase hexadecimal digits");
    }
    if (*kValue == 0) {
        return document::notCanonical(record.pathOf(field), "a field at its default is omitted");
    }
    return *kValue;
}

result::Status fieldOf(const Record& record, const FieldForm& form, NodeFields& fields) {
    switch (form.form) {
    case Form::Real: {
        RAWFRAME_TRY_ASSIGN(const double kValue, record.real(form.name, 0));
        if (!std::isfinite(kValue) || std::abs(kValue) > kMostMagnitude) {
            return invalid(record.pathOf(form.name), "a number is within 4096 of nought");
        }
        store(fields, form.offset, static_cast<float>(kValue));
        return {};
    }
    case Form::Color:
    case Form::Identity: {
        RAWFRAME_TRY_ASSIGN(const std::string_view kText, record.text(form.name));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kValue, hexOf(record, form.name, kText, form.form == Form::Color));
        if (form.form == Form::Color) {
            store(fields, form.offset, static_cast<std::uint32_t>(kValue));
        } else {
            store(fields, form.offset, kValue);
        }
        return {};
    }
    case Form::Truth: {
        RAWFRAME_TRY_ASSIGN(const bool kValue, record.truth(form.name, false));
        store(fields, form.offset, static_cast<std::uint32_t>(kValue ? 1 : 0));
        return {};
    }
    case Form::Whole32:
    case Form::Whole64:
    case Form::Bytes: {
        RAWFRAME_TRY_ASSIGN(const std::int64_t kValue, record.integer(form.name, 0));
        if (form.form == Form::Whole32) {
            if (kValue < std::numeric_limits<std::int32_t>::min() ||
                kValue > std::numeric_limits<std::int32_t>::max()) {
                return invalid(record.pathOf(form.name), "a number past a 32-bit integer's");
            }
            store(fields, form.offset, static_cast<std::int32_t>(kValue));
        } else if (form.form == Form::Bytes) {
            if (kValue < 0 || kValue > kMostEditBytes) {
                return invalid(record.pathOf(form.name), "a field holds from nought to 252 bytes");
            }
            store(fields, form.offset, static_cast<std::uint32_t>(kValue));
        } else {
            store(fields, form.offset, kValue);
        }
        return {};
    }
    case Form::Case: {
        RAWFRAME_TRY_ASSIGN(const std::string_view kName, record.text(form.name));
        const auto kFound = std::ranges::find(form.cases, kName);
        if (kFound == form.cases.end()) {
            return invalid(record.pathOf(form.name), "not a case of the field's set");
        }
        if (kFound == form.cases.begin()) {
            return document::notCanonical(record.pathOf(form.name), "a field at its default is omitted");
        }
        store(fields, form.offset, static_cast<std::uint32_t>(kFound - form.cases.begin()));
        return {};
    }
    }
    return {};
}

/// A game's name for a component: a letter, then letters, digits,
/// underscores, and dots.
bool componentName(std::string_view name, std::size_t longest) noexcept {
    const auto kLetter = [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= 'A' && each <= 'Z');
    };
    return !name.empty() && name.size() <= longest && kLetter(name.front()) &&
           std::ranges::all_of(name, [&kLetter](char each) {
               return kLetter(each) || (each >= '0' && each <= '9') || each == '_' || each == '.';
           });
}

struct Reading {
    const DocumentLimits& limits;
    UiDocument document;
    std::set<std::string, std::less<>> components;

    result::Status
    node(const Value& value, const std::string& path, std::optional<std::size_t> parent, std::size_t depth) {
        if (depth > limits.maximumDepth) {
            return invalid(path, "nodes nested deeper than allowed");
        }
        if (document.nodes.size() >= limits.maximumNodes) {
            return invalid(path, "more nodes than allowed");
        }
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kNodeMembers, path));
        DocumentNode made;
        made.parent = parent;
        RAWFRAME_TRY_ASSIGN(const std::string_view kComponent, kRecord.text("component"));
        if (!componentName(kComponent, limits.maximumNameLength)) {
            return invalid(kRecord.pathOf("component"),
                           "a component's name is a letter, then letters, digits, "
                           "underscores, or dots");
        }
        if (!components.insert(std::string{kComponent}).second) {
            return invalid(kRecord.pathOf("component"), "a component is one node of a document");
        }
        made.component = std::string{kComponent};
        RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kShows, kRecord.optionalText("shows"));
        if (kShows.has_value()) {
            if (!componentName(*kShows, limits.maximumNameLength) || *kShows == kComponent) {
                return invalid(kRecord.pathOf("shows"), "a node shows the words of another component");
            }
            made.shows = std::string{*kShows};
        }
        for (const FieldForm& form : kFieldForms) {
            if (value.find(form.name) != nullptr) {
                RAWFRAME_TRY(fieldOf(kRecord, form, made.fields));
            }
        }
        const std::size_t kIndex = document.nodes.size();
        document.nodes.push_back(std::move(made));
        RAWFRAME_TRY_ASSIGN(const Value* children, kRecord.optional("children", Value::Kind::Array));
        if (children == nullptr) {
            return {};
        }
        if (children->items().empty()) {
            return document::notCanonical(kRecord.pathOf("children"), "a field at its default is omitted");
        }
        for (std::size_t index = 0; index < children->items().size(); ++index) {
            RAWFRAME_TRY(node(children->items()[index], indexed(kRecord.pathOf("children"), index), kIndex, depth + 1));
        }
        return {};
    }
};

} // namespace

result::Result<UiDocument> readDocument(std::string_view text, const DocumentLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Value kRoot, document::parseCanonical(text));
    // The version first: a document of another version is not read further.
    const Value* version = kRoot.find("formatVersion");
    if (version == nullptr || version->integer() != 1) {
        return invalid("formatVersion", "the format version is 1");
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(kRoot, kDocumentFields, "$"));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    if (kKind != "ui.document") {
        return invalid(kRecord.pathOf("kind"), "the kind is ui.document");
    }
    RAWFRAME_TRY_ASSIGN(const Value* nodes, kRecord.required("nodes", Value::Kind::Array));
    Reading reading{.limits = limits, .document = {}, .components = {}};
    for (std::size_t index = 0; index < nodes->items().size(); ++index) {
        RAWFRAME_TRY(reading.node(nodes->items()[index], indexed(kRecord.pathOf("nodes"), index), std::nullopt, 1));
    }
    return std::move(reading.document);
}

} // namespace rawframe::ui
