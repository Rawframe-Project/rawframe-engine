#include "hints.h"
#include "shell.h"

// The Inspector as the screen design draws it (D915): the entity's head,
// and a component's axes as one row of colored X, Y, and Z fields, each
// still its own field to the session.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>

namespace rawframe::studio {

namespace {

/// The runs of fields shown together, and the row's label: a pose's place
/// and turn, a box's size, a plane's place, an emitter's acceleration and
/// a beam's far end and bend.
struct AxisRun {
    std::array<std::string_view, 4> names;
    std::size_t count;
    std::string_view label;
};
constexpr std::array<AxisRun, 9> kRuns = {{{{"x", "y", "z", ""}, 3, "Position"},
                                           {{"qx", "qy", "qz", "qw"}, 4, "Rotation"},
                                           {{"c", "s", "", ""}, 2, "Rotation"},
                                           {{"width", "height", "depth", ""}, 3, "Size"},
                                           {{"angularX", "angularY", "angularZ", ""}, 3, "Angular"},
                                           {{"x", "y", "", ""}, 2, "Position"},
                                           {{"accelerationX", "accelerationY", "accelerationZ", ""}, 3, "Acceleration"},
                                           {{"toX", "toY", "toZ", ""}, 3, "To"},
                                           {{"bendX", "bendY", "bendZ", ""}, 3, "Bend"}}};

/// An emitter's fields as the design's Particles screen groups them
/// (D934): how many leave and from where, how they move, and how they
/// look. The emitter is the one place particles are written, so its
/// component is their editor; no document beside it.
constexpr std::string_view kEmitterType = "rawframe.model.ParticleEmitter";
struct FieldGroup {
    std::string_view label;
    std::array<std::string_view, 8> fields;
};
constexpr std::array<FieldGroup, 3> kEmitterGroups = {
    {{"Spawn", {"rate", "lifetime", "radius", "bursts", "burstCount", "variation", "seed", ""}},
     {"Motion", {"speed", "spread", "accelerationX", "accelerationY", "accelerationZ", "drag", "inherit", ""}},
     {"Look", {"sizeStart", "sizeEnd", "colorStart", "colorEnd", "material", "columns", "rows", "streak"}}}};

std::size_t groupOf(std::string_view field) {
    for (std::size_t each = 0; each < kEmitterGroups.size(); ++each) {
        if (std::ranges::contains(kEmitterGroups[each].fields, field)) {
            return each;
        }
    }
    return kEmitterGroups.size();
}

/// A number as a field shows it: whole bare, else as few digits as it
/// needs.
std::string shownNumber(double value) {
    if (value == std::round(value) && std::abs(value) < 1e15) {
        return std::to_string(static_cast<std::int64_t>(value));
    }
    std::string text = std::to_string(value);
    while (text.ends_with('0')) {
        text.pop_back();
    }
    return text;
}

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

std::array<double, 3> degreesOf(const std::array<double, 4>& quaternion) noexcept {
    const auto [kX, kY, kZ, kW] = quaternion;
    const double kLength = std::sqrt((kX * kX) + (kY * kY) + (kZ * kZ) + (kW * kW));
    if (!(kLength > 0) || !std::isfinite(kLength)) {
        return {};
    }
    const double x = kX / kLength;
    const double y = kY / kLength;
    const double z = kZ / kLength;
    const double w = kW / kLength;
    constexpr double kDegrees = 180 / std::numbers::pi;
    const double kPitch = std::asin(std::clamp(2 * ((w * x) - (y * z)), -1.0, 1.0));
    const double kYaw = std::atan2(2 * ((x * z) + (w * y)), 1 - (2 * ((x * x) + (y * y))));
    const double kRoll = std::atan2(2 * ((x * y) + (w * z)), 1 - (2 * ((x * x) + (z * z))));
    // Shown to a hundredth, without a negative nought.
    const auto kRoundedOf = [](double radians) {
        const double kRounded = std::round(radians * kDegrees * 100) / 100;
        return kRounded == 0 ? 0.0 : kRounded;
    };
    return {kRoundedOf(kPitch), kRoundedOf(kYaw), kRoundedOf(kRoll)};
}

std::array<double, 4> quaternionOf(const std::array<double, 3>& degrees) noexcept {
    constexpr double kHalf = std::numbers::pi / 360;
    const double kCx = std::cos(degrees[0] * kHalf);
    const double kSx = std::sin(degrees[0] * kHalf);
    const double kCy = std::cos(degrees[1] * kHalf);
    const double kSy = std::sin(degrees[1] * kHalf);
    const double kCz = std::cos(degrees[2] * kHalf);
    const double kSz = std::sin(degrees[2] * kHalf);
    // Yaw, then pitch, then roll: qy qx qz.
    return {(kCy * kSx * kCz) + (kSy * kCx * kSz),
            (kSy * kCx * kCz) - (kCy * kSx * kSz),
            (kCy * kCx * kSz) - (kSy * kSx * kCz),
            (kCy * kCx * kCz) + (kSy * kSx * kSz)};
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

result::Status ShellParticipant::vectorRow(const std::string& component,
                                           std::string_view componentName,
                                           std::span<const FieldShown> axes) {
    std::string_view label = "Value";
    for (const AxisRun& kRun : kRuns) {
        if (kRun.count == axes.size() && axes.front().name == kRun.names[0]) {
            label = kRun.label;
            break;
        }
    }
    // A place is a pose's; a velocity's x, y, z its linear part.
    if (axes.front().name == "x" && !componentName.ends_with(".pose")) {
        label = componentName.find("velocity") != std::string_view::npos ? "Linear" : "Value";
    }
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kLine,
        box(componentsColumn_,
            ui::Layout{.alignItems = ui::Align::Center, .gap = 10, .shrink = 0, .padding = {6, 6, 3, 3}},
            0));
    componentRows_.push_back(kLine);
    RAWFRAME_TRY_ASSIGN(const ui::Node kName, box(kLine, ui::Layout{.width = ui::pixels(116), .shrink = 0}, 0));
    RAWFRAME_TRY(words(kName, label, look::kLabelInk, look::kLabelSize));
    RAWFRAME_TRY_ASSIGN(const ui::Node kAxes, box(kLine, ui::Layout{.width = ui::pixels(0), .gap = 4, .grow = 1}, 0));
    // A pose's turn in degrees about X, Y, and Z, its quaternion the
    // session's (D915).
    const bool kTurn = axes.size() == 4 && axes.front().name == "qx" && componentName.ends_with(".pose");
    std::array<double, 3> turned{};
    if (kTurn) {
        std::array<double, 4> quaternion{0, 0, 0, 1};
        for (std::size_t each = 0; each < 4; ++each) {
            if (axes[each].text.has_value()) {
                quaternion[each] = std::strtod(axes[each].text->c_str(), nullptr);
            }
        }
        turned = degreesOf(quaternion);
        held_.turn = turned;
        held_.turnComponent = component;
    }
    // A 2D pose's turn, its cosine and sine, one angle about Z in degrees
    // (D933).
    const bool kFlatTurn = axes.size() == 2 && axes.front().name == "c" && componentName.ends_with(".pose");
    double flatAngle = 0;
    if (kFlatTurn) {
        const double kC = axes[0].text.has_value() ? std::strtod(axes[0].text->c_str(), nullptr) : 1.0;
        const double kS = axes[1].text.has_value() ? std::strtod(axes[1].text->c_str(), nullptr) : 0.0;
        flatAngle = std::atan2(kS, kC) * 180.0 / std::numbers::pi;
        if (std::abs(flatAngle) < 1e-9) {
            flatAngle = 0;
        }
    }
    const std::size_t kShownAxes = kTurn ? 3 : kFlatTurn ? 1 : axes.size();
    constexpr std::array<std::uint32_t, 4> kInks = {look::kAxisX, look::kAxisY, look::kAxisZ, look::kLabelInk};
    constexpr std::array<std::string_view, 4> kLetters = {"X", "Y", "Z", "W"};
    for (std::size_t each = 0; each < kShownAxes; ++each) {
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
        RAWFRAME_TRY(tree_->setLook(kTag, ui::Look{.fill = kFlatTurn ? look::kAxisZ : kInks[each]}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kLetter, box(kTag, ui::Layout{}, 0));
        RAWFRAME_TRY(monoWords(kLetter, kFlatTurn ? "Z" : kLetters[each], 0x0B0C0FFFU, look::kSmallSize));
        RAWFRAME_TRY(tree_->setInteraction(kTag, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kValue, tree_->addEditable(++keys_));
        RAWFRAME_TRY(tree_->setLayout(
            kValue,
            ui::Layout{.width = ui::pixels(0), .minHeight = ui::pixels(23), .grow = 1, .padding = {6, 4, 3, 3}}));
        RAWFRAME_TRY(tree_->setLook(kValue, ui::Look{}));
        RAWFRAME_TRY(tree_->attach(kWell, kValue));
        const bool kSet = kTurn || kFlatTurn ? std::ranges::any_of(axes,
                                                                   [](const FieldShown& field) {
                                                                       return field.text.has_value();
                                                                   })
                                             : kAxis.text.has_value();
        RAWFRAME_TRY(monoWords(kValue,
                               kTurn       ? shownNumber(turned[each])
                               : kFlatTurn ? shownNumber(flatAngle)
                                           : kAxis.text.value_or(std::string{kAxis.name == "qw" ? "1" : "0"}),
                               kSet ? look::kInk : look::kFaintInk,
                               look::kLabelSize));
        RAWFRAME_TRY(tree_->setRole(kValue, ui::Role::TextInput));
        const std::string kFieldName = kTurn ? std::string{kLetters[each] == "X"   ? "x"
                                                           : kLetters[each] == "Y" ? "y"
                                                                                   : "z"}
                                             : kAxis.name;
        RAWFRAME_TRY(tree_->setName(kValue,
                                    kFlatTurn ? std::string{"turn z"}
                                    : kTurn   ? "turn " + kFieldName
                                              : kAxis.name));
        const Catalog::Field kType = kAxis.type.value_or(Catalog::Field{.name = kAxis.name, .kind = kAxis.kind});
        fields_.push_back(FieldRow{.value = kValue,
                                   .role = kTurn || kFlatTurn ? FieldRow::Role::Turn : FieldRow::Role::Field,
                                   .component = component,
                                   .field = kFlatTurn ? std::string{"angle"} : kFieldName,
                                   .kind = kAxis.kind,
                                   .type = kType});
    }
    // An instance's own values, each of which it may drop for its
    // source's, under the row (D443, D925).
    const bool kBrought = entityAt_ < brought_.size() && brought_[entityAt_];
    if (kBrought && !kTurn && !kFlatTurn && catalog_.offers("scene.revert_field") &&
        std::ranges::any_of(axes, [](const FieldShown& field) {
            return field.text.has_value();
        })) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kReverts,
                            box(componentsColumn_,
                                ui::Layout{.alignItems = ui::Align::Center, .shrink = 0, .padding = {132, 6, 0, 2}},
                                0));
        componentRows_.push_back(kReverts);
        for (const FieldShown& kAxis : axes) {
            if (kAxis.text.has_value()) {
                RAWFRAME_TRY(action(kReverts,
                                    "Revert " + kAxis.name,
                                    ActionButton{.operation = "scene.revert_field",
                                                 .component = component,
                                                 .field = kAxis.name,
                                                 .done = kAxis.name + " reverted"}));
            }
        }
    }
    return {};
}

std::vector<FieldShown> ShellParticipant::groupedFields(const std::string& component,
                                                        std::vector<FieldShown> fields) const {
    if (const Catalog::Component* kType = catalog_.component(component);
        kType != nullptr && kType->type == kEmitterType) {
        std::ranges::stable_sort(fields, {}, [](const FieldShown& each) {
            return groupOf(each.name);
        });
    }
    return fields;
}

void ShellParticipant::fieldGroupRow(const std::string& component,
                                     std::span<const FieldShown> fields,
                                     std::size_t place) {
    const Catalog::Component* kType = catalog_.component(component);
    if (kType == nullptr || place >= fields.size()) {
        return;
    }
    const std::string& kName = fields[place].name;
    // A group's name over its first field.
    if (const std::size_t kGroup = groupOf(kName); kType->type == kEmitterType && kGroup < kEmitterGroups.size() &&
                                                   (place == 0 || groupOf(fields[place - 1].name) != kGroup)) {
        if (auto heading = box(componentsColumn_, ui::Layout{.shrink = 0, .padding = {12, 10, 10, 2}}, 0);
            heading.has_value()) {
            componentRows_.push_back(*heading);
            static_cast<void>(strongWords(*heading, kEmitterGroups[kGroup].label, look::kSoftInk, look::kLabelSize));
        }
    }
    // Over a start and an end color, the colors a particle, a trail, or a
    // beam passes through, as the design's Color over life.
    const auto kEnd = std::ranges::find(fields, std::string_view{"colorEnd"}, &FieldShown::name);
    if (kName != "colorStart" || kEnd == fields.end()) {
        return;
    }
    const auto kColor = [](const FieldShown& field) {
        const Catalog::Field kColorType = field.type.value_or(Catalog::Field{.name = field.name, .kind = field.kind});
        return colorOf(shownAs(kColorType, field.text.value_or("4294967295"))).value_or(0xFFFFFFFFU);
    };
    auto line = box(componentsColumn_,
                    ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0, .padding = {10, 10, 4, 4}},
                    0);
    if (!line.has_value()) {
        return;
    }
    componentRows_.push_back(*line);
    if (auto label = box(*line, ui::Layout{.width = ui::pixels(116), .shrink = 0}, 0); label.has_value()) {
        static_cast<void>(words(*label, "Color over life", look::kLabelInk, look::kLabelSize));
    }
    if (auto bar = box(*line, ui::Layout{.height = ui::pixels(20), .grow = 1}, 0); bar.has_value()) {
        static_cast<void>(tree_->setName(*bar, "Color over life"));
        static_cast<void>(
            tree_->setLook(*bar,
                           ui::Look{.fill = 0x0B0C0FFFU,
                                    .borderColor = look::kEdge,
                                    .radius = 4,
                                    .gradient = ui::GradientLook{.kind = ui::GradientLook::Kind::Linear,
                                                                 .angle = 90,
                                                                 .colors = {kColor(fields[place]), kColor(*kEnd), 0, 0},
                                                                 .positions = {0, 1, 0, 0},
                                                                 .stops = 2}}));
    }
}

void ShellParticipant::asCheckbox(ui::Node value, bool on) {
    // A truth as the design's box (D916): checked, the accent with its
    // mark; its words kept, unseen, as the value read and toggled.
    static_cast<void>(tree_->setLayout(
        value, ui::Layout{.width = ui::pixels(16), .height = ui::pixels(16), .shrink = 0, .border = {1, 1, 1, 1}}));
    static_cast<void>(tree_->setLook(value,
                                     ui::Look{.fill = on ? look::kAccent : look::kWellFill,
                                              .borderColor = on ? look::kAccent : look::kRaisedEdge,
                                              .radius = 3,
                                              .clip = true,
                                              .image = on ? look::iconImage(Icon::Check) : 0,
                                              .imageTint = look::kOnAccent}));
    static_cast<void>(words(value, on ? "true" : "false", 0, 1));
    static_cast<void>(tree_->setRole(value, ui::Role::CheckBox));
}

void ShellParticipant::asChooser(ui::Node line) {
    // A field of a few values shows the chevron its list opens from.
    if (auto chevron = icon(line, Icon::ChevronDown, look::kLabelInk, 12); chevron.has_value()) {
        // Drawn over the well's end, pulled back over the line's gap.
        static_cast<void>(tree_->setLayout(
            *chevron,
            ui::Layout{.width = ui::pixels(12), .height = ui::pixels(12), .shrink = 0, .margin = {-30, 18, 0, 0}}));
    }
}

void ShellParticipant::showNothingChosen() {
    // With nothing chosen in a scene, the Inspector says how to choose or
    // make something, as the design's empty state (D922).
    if (!componentRows_.empty() || scene_.empty() || documentChosen() || !entity_.empty() || !browser_.chosen.empty() ||
        live_.on || !tree_->contains(componentsColumn_)) {
        return;
    }
    auto made = box(componentsColumn_,
                    ui::Layout{.direction = ui::Direction::Column,
                               .alignItems = ui::Align::Center,
                               .gap = 8,
                               .shrink = 0,
                               .padding = {16, 16, 48, 16}},
                    0);
    if (!made.has_value()) {
        return;
    }
    componentRows_.push_back(*made);
    if (auto tile = box(*made,
                        ui::Layout{.width = ui::pixels(44),
                                   .height = ui::pixels(44),
                                   .justify = ui::Justify::Center,
                                   .alignItems = ui::Align::Center,
                                   .shrink = 0},
                        0);
        tile.has_value()) {
        static_cast<void>(tree_->setLook(*tile, ui::Look{.fill = look::kRaisedFill, .radius = 8}));
        static_cast<void>(icon(*tile, Icon::Select, look::kLabelInk, 20));
    }
    if (auto title = box(*made, ui::Layout{.shrink = 0, .margin = {0, 0, 6, 0}}, 0); title.has_value()) {
        static_cast<void>(strongWords(*title, "Nothing chosen", look::kInk, 14));
    }
    if (auto what = box(*made, ui::Layout{.shrink = 0}, 0); what.has_value()) {
        static_cast<void>(wrappedWords(
            *what, "Choose an entity in the Hierarchy or the viewport,", look::kLabelInk, look::kLabelSize));
    }
    if (auto what = box(*made, ui::Layout{.shrink = 0}, 0); what.has_value()) {
        static_cast<void>(words(*what, "or make one:", look::kLabelInk, look::kLabelSize));
    }
    if (auto buttons = box(*made, ui::Layout{.gap = 6, .shrink = 0, .margin = {0, 0, 8, 0}, .wrap = true}, 0);
        buttons.has_value()) {
        held_.emptyButtons.clear();
        for (const std::string& kCommand : partCommands()) {
            if (kCommand != "New cube" && kCommand != "New sphere" && kCommand != "New lamp" &&
                kCommand != "New empty") {
                continue;
            }
            std::string label = kCommand.substr(4);
            label[0] = static_cast<char>(label[0] - 'a' + 'A');
            if (auto button = textButton(*buttons, Icon::Plus, label); button.has_value()) {
                held_.emptyButtons.emplace_back(*button, kCommand);
            }
        }
    }
}

} // namespace rawframe::studio
