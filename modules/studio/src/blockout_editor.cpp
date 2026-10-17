#include "rawframe/blockout/blockout.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace rawframe::studio {

namespace {

/// A cut's footprint in the plan, apart from the solids it takes from.
constexpr std::uint32_t kCutFill = 0x7A3B3BFFU;
/// The plan's margin, and the widest side it is drawn to, in pixels; a
/// meter is drawn no smaller than 4 pixels and no larger than 64.
constexpr float kPlanMargin = 16;
constexpr float kPlanSide = 560;

/// A line's words, split on spaces and tabs, its comment left out.
std::vector<std::string> wordsOf(const std::string& line) {
    std::vector<std::string> words;
    std::string word;
    for (const char kEach : line.substr(0, line.find('#'))) {
        if (kEach == ' ' || kEach == '\t' || kEach == '\r') {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
        } else {
            word += kEach;
        }
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }
    return words;
}

/// A solid's line as Studio shows it (D850): whether it cuts, its shape,
/// its name, and the words after each word the format reads (D846).
struct SolidLine {
    bool cut = false;
    std::string shape;
    std::string name;
    std::vector<std::pair<std::string, std::string>> words;
};

constexpr std::array<std::string_view, 3> kBoxWords = {"at", "size", "turn"};
constexpr std::array<std::string_view, 4> kCylinderWords = {"at", "radius", "height", "sides"};
constexpr std::array<std::string_view, 4> kExtrudeWords = {"at", "height", "turn", "points"};

/// The words a shape reads, in the order a line is written.
std::span<const std::string_view> wordsFor(std::string_view shape) {
    if (shape == "cylinder") {
        return kCylinderWords;
    }
    if (shape == "extrude") {
        return kExtrudeWords;
    }
    return kBoxWords;
}

/// The solid `line` is, or none for the header, a comment, or a blank.
std::optional<SolidLine> solidOf(const std::string& line) {
    const std::vector<std::string> kWords = wordsOf(line);
    SolidLine made;
    std::size_t at = 0;
    if (at < kWords.size() && kWords[at] == "cut") {
        made.cut = true;
        ++at;
    }
    if (at + 1 >= kWords.size()) {
        return std::nullopt;
    }
    made.shape = kWords[at++];
    if (made.shape != "box" && made.shape != "ramp" && made.shape != "cylinder" && made.shape != "extrude") {
        return std::nullopt;
    }
    made.name = kWords[at++];
    const std::span<const std::string_view> kReads = wordsFor(made.shape);
    for (; at < kWords.size(); ++at) {
        if (std::ranges::contains(kReads, std::string_view{kWords[at]})) {
            made.words.emplace_back(kWords[at], std::string{});
        } else if (!made.words.empty()) {
            std::string& value = made.words.back().second;
            value += (value.empty() ? "" : " ") + kWords[at];
        }
    }
    return made;
}

/// The line `solid` is written as, its words in the format's order.
std::string lineOf(const SolidLine& solid) {
    std::string made = (solid.cut ? "cut " : "") + solid.shape + " " + solid.name;
    for (const std::string_view kWord : wordsFor(solid.shape)) {
        const auto kHeld = std::ranges::find(solid.words, kWord, &std::pair<std::string, std::string>::first);
        if (kHeld != solid.words.end() && !kHeld->second.empty()) {
            made += " " + kHeld->first + " " + kHeld->second;
        }
    }
    return made;
}

} // namespace

void ShellParticipant::showBlockout() {
    // A blockout is its lines (D849), each solid a row by its name, a line
    // typed above them put in as a solid after the last.
    const std::optional<Value> kRead = read(scene_, "lines.read");
    const Value* kLines = kRead.has_value() ? kRead->find("lines") : nullptr;
    held_.blockoutLines.clear();
    if (kLines != nullptr && kLines->kind() == Value::Kind::Array) {
        for (const Value& kLine : kLines->items()) {
            held_.blockoutLines.push_back(kLine.text() != nullptr ? *kLine.text() : std::string{});
        }
    }
    if (auto line = fieldRow("new solid", "", entitiesColumn_); line.has_value()) {
        sceneFieldRows_.push_back(line->first);
        sceneFields_.push_back(FieldRow{.value = line->second,
                                        .role = FieldRow::Role::NewSolid,
                                        .field = std::to_string(held_.blockoutLines.size())});
    }
    std::size_t solids = 0;
    for (const std::string& kLine : held_.blockoutLines) {
        const std::optional<SolidLine> kSolid = solidOf(kLine);
        if (!kSolid.has_value() || entityRows_.size() >= EditorsHeld::kMostRows) {
            continue;
        }
        const std::string kLabel = kSolid->name + " (" + (kSolid->cut ? "cut " : "") + kSolid->shape + ")";
        auto added = row(entitiesColumn_, kLabel, kText, kRow, true);
        if (!added.has_value()) {
            return;
        }
        entityRows_.push_back(*added);
        entities_.push_back(kSolid->name);
        names_.push_back(kLabel);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
        ++solids;
    }
    showPlan();
    emitter_.log(diagnostics::Severity::Info,
                 kBlockoutShown,
                 "a blockout shown, its solids as rows",
                 {diagnostics::field("blockout", scene_),
                  diagnostics::field("lines", held_.blockoutLines.size()),
                  diagnostics::field("solids", solids)});
}

void ShellParticipant::showPlan() {
    clear(viewRows_);
    viewFields_.clear();
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
    // The solids as the blockout module reads them, each where it stands
    // seen from above (D851), +X to the right and +Z down; a document that
    // does not read draws no plan.
    std::string text;
    for (const std::string& kLine : held_.blockoutLines) {
        text += kLine + "\n";
    }
    const auto kRead = blockout::parseBlockout(text);
    if (!kRead.has_value() || kRead->solids.empty()) {
        return;
    }
    std::vector<blockout::Footprint> footprints;
    blockout::Footprint all = blockout::footprintOf(kRead->solids[0]);
    for (const blockout::Solid& kSolid : kRead->solids) {
        const blockout::Footprint kEach = blockout::footprintOf(kSolid);
        footprints.push_back(kEach);
        all = {.minX = std::min(all.minX, kEach.minX),
               .minZ = std::min(all.minZ, kEach.minZ),
               .maxX = std::max(all.maxX, kEach.maxX),
               .maxZ = std::max(all.maxZ, kEach.maxZ)};
    }
    const double kWide = all.maxX - all.minX;
    const double kDeep = all.maxZ - all.minZ;
    const float kScale = std::clamp(static_cast<float>(kPlanSide / std::max({kWide, kDeep, 1.0})), 4.0F, 64.0F);
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    held_.planScale = kScale;
    static_cast<void>(tree_->setName(*pane, "Plan"));
    auto sheet = box(*pane,
                     ui::Layout{.width = ui::pixels((static_cast<float>(kWide) * kScale) + (2 * kPlanMargin)),
                                .height = ui::pixels((static_cast<float>(kDeep) * kScale) + (2 * kPlanMargin)),
                                .shrink = 0},
                     kBackground);
    if (!sheet.has_value()) {
        return;
    }
    // Solids in the document's order, so a cut lies over what it takes
    // from; each pressed chooses its row.
    std::size_t cuts = 0;
    for (std::size_t each = 0; each < kRead->solids.size(); ++each) {
        const blockout::Solid& kSolid = kRead->solids[each];
        const auto kListed = std::ranges::find(entities_, kSolid.name);
        if (kListed == entities_.end()) {
            continue;
        }
        const std::size_t kEntity = static_cast<std::size_t>(kListed - entities_.begin());
        const blockout::Footprint& kAt = footprints[each];
        const auto kPixels = [kScale](double meters) {
            return std::max(static_cast<float>(meters) * kScale, 1.0F);
        };
        auto drawn =
            box(*sheet,
                ui::Layout{.width = ui::pixels(kPixels(kAt.maxX - kAt.minX)),
                           .height = ui::pixels(kPixels(kAt.maxZ - kAt.minZ)),
                           .shrink = 0,
                           .placement =
                               ui::Placement{
                                   .absolute = true,
                                   .x = ui::pixels(kPlanMargin + (static_cast<float>(kAt.minX - all.minX) * kScale)),
                                   .y = ui::pixels(kPlanMargin + (static_cast<float>(kAt.minZ - all.minZ) * kScale))}},
                kSolid.cut ? kCutFill : (kSolid.name == entity_ ? kChosen : kRow));
        if (!drawn.has_value() || !tree_->setRole(*drawn, ui::Role::Button).has_value() ||
            !tree_->setName(*drawn, kSolid.name + " plan").has_value()) {
            return;
        }
        held_.canvasParts.emplace_back(*drawn, kEntity);
        if (kSolid.cut) {
            ++cuts;
        } else {
            held_.canvasBoxes.emplace_back(*drawn, kEntity);
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kPlanShown,
                 "a blockout's solids drawn from above",
                 {diagnostics::field("solids", held_.canvasParts.size()),
                  diagnostics::field("cuts", cuts),
                  diagnostics::field("centimetersWide", static_cast<std::int64_t>(std::lround(kWide * 100))),
                  diagnostics::field("centimetersDeep", static_cast<std::int64_t>(std::lround(kDeep * 100)))});
}

void ShellParticipant::showBlockoutSolid(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    markCanvas();
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const auto kLine = std::ranges::find_if(held_.blockoutLines, [this](const std::string& each) {
        const std::optional<SolidLine> kSolid = solidOf(each);
        return kSolid.has_value() && kSolid->name == entity_;
    });
    if (kLine == held_.blockoutLines.end()) {
        return;
    }
    const SolidLine kSolid = *solidOf(*kLine);
    if (auto heading = row(componentsColumn_, names_[at], kQuiet, kPanel); heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    // Each word its shape reads a field holding the numbers after it, those
    // it leaves out empty; then the whole line, which an empty field takes
    // away.
    const auto kWordField = [this](const std::string& label, const std::string& value) {
        auto line = fieldRow(label, value);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::SolidWord, .field = label});
    };
    for (const std::string_view kWord : wordsFor(kSolid.shape)) {
        const auto kHeld = std::ranges::find(kSolid.words, kWord, &std::pair<std::string, std::string>::first);
        kWordField(std::string{kWord}, kHeld != kSolid.words.end() ? kHeld->second : std::string{});
    }
    kWordField("line", *kLine);
    ++components_;
}

void ShellParticipant::dropSolid(std::size_t at, std::array<float, 2> moved) {
    // Its footprint carried across the plan moves the solid as far, at the
    // plan's scale, on a quarter-meter grid; its height stays (D852).
    if (at >= entities_.size()) {
        return;
    }
    const std::string kName = entities_[at];
    const auto kLine = std::ranges::find_if(held_.blockoutLines, [&kName](const std::string& each) {
        const std::optional<SolidLine> kSolid = solidOf(each);
        return kSolid.has_value() && kSolid->name == kName;
    });
    if (kLine == held_.blockoutLines.end()) {
        return;
    }
    SolidLine solid = *solidOf(*kLine);
    const auto kAt = std::ranges::find(solid.words, std::string{"at"}, &std::pair<std::string, std::string>::first);
    if (kAt == solid.words.end()) {
        return;
    }
    std::array<double, 3> place{};
    const char* reading = kAt->second.c_str();
    for (double& each : place) {
        char* end = nullptr;
        each = std::strtod(reading, &end);
        if (end == reading) {
            return;
        }
        reading = end;
    }
    const auto kSnapped = [](double meters) {
        return std::round(meters * 4) / 4;
    };
    place[0] = kSnapped(place[0] + (moved[0] / held_.planScale));
    place[2] = kSnapped(place[2] + (moved[1] / held_.planScale));
    const auto kNumber = [](double value) {
        char made[32];
        std::snprintf(made, sizeof made, "%.6g", value == 0 ? 0.0 : value);
        return std::string{made};
    };
    kAt->second = kNumber(place[0]) + " " + kNumber(place[1]) + " " + kNumber(place[2]);
    Value operation = Value::object();
    operation.add("operation", Value::string("lines.set"));
    operation.add("line", Value::integer(static_cast<std::int64_t>(kLine - held_.blockoutLines.begin())));
    operation.add("text", Value::string(lineOf(solid)));
    commit(std::move(operation), kName + " moved to " + kAt->second, kName);
}

void ShellParticipant::setSolid(const FieldRow& row, const std::string& text) {
    Value operation = Value::object();
    if (row.role == FieldRow::Role::NewSolid) {
        const std::optional<SolidLine> kSolid = solidOf(text);
        if (text.empty()) {
            return;
        }
        operation.add("operation", Value::string("lines.insert"));
        operation.add("line", Value::integer(std::strtoll(row.field.c_str(), nullptr, 10)));
        operation.add("text", Value::string(text));
        commit(std::move(operation),
               (kSolid.has_value() ? kSolid->name : text) + " added",
               kSolid.has_value() ? std::optional{kSolid->name} : std::nullopt);
        return;
    }
    // The chosen solid's line, found by its name as it was read.
    const auto kLine = std::ranges::find_if(held_.blockoutLines, [this](const std::string& each) {
        const std::optional<SolidLine> kSolid = solidOf(each);
        return kSolid.has_value() && kSolid->name == entity_;
    });
    if (kLine == held_.blockoutLines.end()) {
        return;
    }
    std::string written = text;
    if (row.field != "line") {
        SolidLine solid = *solidOf(*kLine);
        const auto kHeld = std::ranges::find(solid.words, row.field, &std::pair<std::string, std::string>::first);
        if (kHeld != solid.words.end()) {
            kHeld->second = text;
        } else {
            solid.words.emplace_back(row.field, text);
        }
        written = lineOf(solid);
    }
    operation.add("operation", Value::string("lines.set"));
    operation.add("line", Value::integer(static_cast<std::int64_t>(kLine - held_.blockoutLines.begin())));
    operation.add("text", written.empty() ? Value{} : Value::string(written));
    const std::optional<SolidLine> kAfter = solidOf(written);
    commit(std::move(operation),
           written.empty() ? entity_ + " taken away" : entity_ + " " + row.field + " set to " + text,
           kAfter.has_value() ? std::optional{kAfter->name} : std::nullopt);
}

} // namespace rawframe::studio
