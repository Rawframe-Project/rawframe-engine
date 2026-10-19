#include "shell.h"

// The Inspector as the screen design draws it (D915): the entity's head,
// and a component's axes as one row of colored X, Y, and Z fields, each
// still its own field to the session.

#include <algorithm>
#include <array>
#include <cctype>

namespace rawframe::studio {

namespace {

/// The runs of fields shown together, and the row's label: a pose's place
/// and turn, a box's size, a plane's place.
struct AxisRun {
    std::array<std::string_view, 4> names;
    std::size_t count;
    std::string_view label;
};
constexpr std::array<AxisRun, 4> kRuns = {{{{"x", "y", "z", ""}, 3, "Position"},
                                           {{"qx", "qy", "qz", "qw"}, 4, "Rotation"},
                                           {{"width", "height", "depth", ""}, 3, "Size"},
                                           {{"x", "y", "", ""}, 2, "Position"}}};

bool numeric(const FieldShown& field) {
    return field.kind == "real" || field.kind == "signed" || field.kind == "unsigned";
}

} // namespace

std::string prettyField(std::string_view name) {
    // camelCase and snake_case read as words, the first capital.
    std::string made;
    for (std::size_t at = 0; at < name.size(); ++at) {
        const char kAt = name[at];
        if (kAt == '_') {
            made += ' ';
            continue;
        }
        if (at > 0 && std::isupper(static_cast<unsigned char>(kAt)) != 0 &&
            std::islower(static_cast<unsigned char>(name[at - 1])) != 0) {
            made += ' ';
            made += static_cast<char>(std::tolower(static_cast<unsigned char>(kAt)));
            continue;
        }
        made += kAt;
    }
    if (!made.empty()) {
        made[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(made[0])));
    }
    return made;
}

std::string prettyComponent(std::string_view name) {
    // A component by its own last part; a pose is the entity's transform.
    const std::size_t kDot = name.rfind('.');
    const std::string_view kLast = kDot == std::string_view::npos ? name : name.substr(kDot + 1);
    if (kLast == "pose") {
        return "Transform";
    }
    return prettyField(kLast);
}

result::Result<std::optional<ui::Node>>
ShellParticipant::inspectorHead(std::string_view name, bool brought, bool renamed) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kHead,
                        box(componentsColumn_,
                            ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0, .padding = {6, 6, 4, 8}},
                            0));
    componentRows_.push_back(kHead);
    RAWFRAME_TRY_ASSIGN(const ui::Node kTile,
                        box(kHead,
                            ui::Layout{.width = ui::pixels(30),
                                       .height = ui::pixels(30),
                                       .justify = ui::Justify::Center,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0},
                            0));
    RAWFRAME_TRY(tree_->setLook(kTile, ui::Look{.fill = brought ? 0x3A2D5CFFU : look::kSelected, .radius = 6}));
    RAWFRAME_TRY(icon(kTile, brought ? Icon::Prefab : Icon::Cube, brought ? 0xC9B2FFFFU : 0xA8C6FFFFU, 16));
    if (!renamed) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kName, box(kHead, ui::Layout{.grow = 1}, 0));
        RAWFRAME_TRY(strongWords(kName, name, look::kInk, 14));
        return std::optional<ui::Node>{};
    }
    RAWFRAME_TRY_ASSIGN(const ui::Node kValue, tree_->addEditable(++keys_));
    RAWFRAME_TRY(tree_->setLayout(kValue,
                                  ui::Layout{.width = ui::pixels(0),
                                             .minHeight = ui::pixels(28),
                                             .grow = 1,
                                             .padding = {8, 8, 4, 4},
                                             .border = {1, 1, 1, 1}}));
    RAWFRAME_TRY(tree_->setLook(kValue, ui::Look{.fill = look::kWellFill, .borderColor = look::kEdge, .radius = 4}));
    RAWFRAME_TRY(tree_->attach(kHead, kValue));
    RAWFRAME_TRY(strongWords(kValue, name, look::kInk, 14));
    RAWFRAME_TRY(tree_->setRole(kValue, ui::Role::TextInput));
    RAWFRAME_TRY(tree_->setName(kValue, "name"));
    return std::optional<ui::Node>{kValue};
}

std::size_t ShellParticipant::axesAt(std::span<const FieldShown> shown, std::size_t place) {
    for (const AxisRun& kRun : kRuns) {
        if (place + kRun.count > shown.size()) {
            continue;
        }
        bool matches = true;
        for (std::size_t each = 0; each < kRun.count && matches; ++each) {
            matches = shown[place + each].name == kRun.names[each] && numeric(shown[place + each]);
        }
        if (matches) {
            return kRun.count;
        }
    }
    return 0;
}

result::Status ShellParticipant::vectorRow(const std::string& component, std::span<const FieldShown> axes) {
    std::string_view label = "Value";
    for (const AxisRun& kRun : kRuns) {
        if (kRun.count == axes.size() && axes.front().name == kRun.names[0]) {
            label = kRun.label;
        }
    }
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kLine,
        box(componentsColumn_,
            ui::Layout{.alignItems = ui::Align::Center, .gap = 10, .shrink = 0, .padding = {6, 6, 3, 3}},
            0));
    componentRows_.push_back(kLine);
    RAWFRAME_TRY_ASSIGN(const ui::Node kName, box(kLine, ui::Layout{.width = ui::pixels(96), .shrink = 0}, 0));
    RAWFRAME_TRY(words(kName, label, look::kLabelInk, look::kLabelSize));
    RAWFRAME_TRY_ASSIGN(const ui::Node kAxes, box(kLine, ui::Layout{.width = ui::pixels(0), .gap = 4, .grow = 1}, 0));
    constexpr std::array<std::uint32_t, 4> kInks = {look::kAxisX, look::kAxisY, look::kAxisZ, look::kLabelInk};
    constexpr std::array<std::string_view, 4> kLetters = {"X", "Y", "Z", "W"};
    for (std::size_t each = 0; each < axes.size(); ++each) {
        const FieldShown& kAxis = axes[each];
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kWell,
            box(kAxes,
                ui::Layout{.width = ui::pixels(0), .alignItems = ui::Align::Stretch, .grow = 1, .border = {1, 1, 1, 1}},
                0));
        RAWFRAME_TRY(tree_->setLook(
            kWell, ui::Look{.fill = look::kWellFill, .borderColor = look::kEdge, .radius = 4, .clip = true}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kTag,
                            box(kWell,
                                ui::Layout{.width = ui::pixels(16),
                                           .justify = ui::Justify::Center,
                                           .alignItems = ui::Align::Center,
                                           .shrink = 0},
                                0));
        RAWFRAME_TRY(tree_->setLook(kTag, ui::Look{.fill = kInks[each]}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kLetter, box(kTag, ui::Layout{}, 0));
        RAWFRAME_TRY(monoWords(kLetter, kLetters[each], 0x0B0C0FFFU, look::kSmallSize));
        RAWFRAME_TRY(tree_->setInteraction(kTag, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kValue, tree_->addEditable(++keys_));
        RAWFRAME_TRY(tree_->setLayout(
            kValue,
            ui::Layout{.width = ui::pixels(0), .minHeight = ui::pixels(23), .grow = 1, .padding = {6, 4, 3, 3}}));
        RAWFRAME_TRY(tree_->setLook(kValue, ui::Look{}));
        RAWFRAME_TRY(tree_->attach(kWell, kValue));
        RAWFRAME_TRY(monoWords(kValue,
                               kAxis.text.value_or(std::string{}),
                               kAxis.text.has_value() ? look::kInk : look::kFaintInk,
                               look::kLabelSize));
        RAWFRAME_TRY(tree_->setRole(kValue, ui::Role::TextInput));
        RAWFRAME_TRY(tree_->setName(kValue, kAxis.name));
        const Catalog::Field kType = kAxis.type.value_or(Catalog::Field{.name = kAxis.name, .kind = kAxis.kind});
        fields_.push_back(
            FieldRow{.value = kValue, .component = component, .field = kAxis.name, .kind = kAxis.kind, .type = kType});
    }
    return {};
}

} // namespace rawframe::studio
