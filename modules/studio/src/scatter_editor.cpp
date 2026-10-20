#include "rawframe/scatter/scatter.h"
#include "rawframe/terrain/terrain.h"
#include "shell.h"

#include <algorithm>
#include <cstdlib>

namespace rawframe::studio {

namespace {

/// The plan's widest side, in pixels, and a copy's dot.
constexpr float kPlanSide = 560;
constexpr float kDot = 6;
constexpr std::uint32_t kDotFill = 0x9CC98AFFU;

/// A line's first word, and what follows it.
std::string wordOf(const std::string& line) {
    const std::size_t kStart = line.find_first_not_of(" \t");
    if (kStart == std::string::npos || line[kStart] == '#') {
        return {};
    }
    return line.substr(kStart, line.find_first_of(" \t", kStart) - kStart);
}

std::string restOfLine(const std::string& line) {
    const std::size_t kWord = line.find_first_not_of(" \t");
    const std::size_t kGap = kWord == std::string::npos ? std::string::npos : line.find_first_of(" \t", kWord);
    const std::size_t kRest = kGap == std::string::npos ? std::string::npos : line.find_first_not_of(" \t", kGap);
    return kRest == std::string::npos ? std::string{} : line.substr(kRest);
}

/// The path, under the game's directory, of `named` beside `document`.
std::string beside(const std::string& document, const std::string& named) {
    const std::size_t kSlash = document.rfind('/');
    return kSlash == std::string::npos ? named : document.substr(0, kSlash + 1) + named;
}

} // namespace

void ShellParticipant::showScatter() {
    // A scatter is its lines (D865): each a field of the Inspector, its
    // copies drawn over its area in the center as the engine places them,
    // and a row that draws them again from the next seed.
    const std::optional<Value> kRead = read(scene_, "lines.read");
    const Value* kLines = kRead.has_value() ? kRead->find("lines") : nullptr;
    held_.scatterLines.clear();
    std::string text;
    if (kLines != nullptr && kLines->kind() == Value::Kind::Array) {
        for (const Value& kLine : kLines->items()) {
            held_.scatterLines.push_back(kLine.text() != nullptr ? *kLine.text() : std::string{});
            text += held_.scatterLines.back() + "\n";
        }
    }
    if (auto added = row(entitiesColumn_, "Reseed", kText, kRow, true); added.has_value()) {
        entityRows_.push_back(*added);
        entities_.push_back("reseed");
        names_.push_back("Reseed");
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
    showScatterLines();
    clear(viewRows_);
    viewFields_.clear();
    clear(held_.cameraRows);
    const auto kScatter = scatter::parseScatter(text);
    if (!kScatter.has_value()) {
        return;
    }
    // The ground it stands on, read through the session as its own lines.
    std::optional<terrain::Terrain> ground;
    if (!kScatter->terrain.empty()) {
        const std::optional<Value> kGround = read(beside(scene_, kScatter->terrain), "lines.read");
        const Value* kGroundLines = kGround.has_value() ? kGround->find("lines") : nullptr;
        std::string groundText;
        if (kGroundLines != nullptr && kGroundLines->kind() == Value::Kind::Array) {
            for (const Value& kLine : kGroundLines->items()) {
                groundText += (kLine.text() != nullptr ? *kLine.text() : std::string{}) + "\n";
            }
        }
        if (auto parsed = terrain::parseTerrain(groundText); parsed.has_value()) {
            ground = std::move(*parsed);
        }
    }
    const auto kPlaced = scatter::placements(*kScatter, ground.has_value() ? &*ground : nullptr);
    const std::size_t kCopies = kPlaced.has_value() ? kPlaced->size() : 0;
    const double kScale = kPlanSide / std::max(kScatter->width, kScatter->depth);
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Scatter"));
    auto area = box(*pane,
                    ui::Layout{.width = ui::pixels(static_cast<float>(kScatter->width * kScale) + kDot),
                               .height = ui::pixels(static_cast<float>(kScatter->depth * kScale) + kDot),
                               .shrink = 0},
                    kPanel);
    if (!area.has_value()) {
        return;
    }
    // Each copy a dot where it stands, the area's least corner at the top
    // left.
    for (const scatter::Placement& kEach : kPlaced.value_or(std::vector<scatter::Placement>{})) {
        const auto kX = static_cast<float>((kEach.x - kScatter->x) * kScale);
        const auto kY = static_cast<float>((kEach.z - kScatter->z) * kScale);
        auto dot =
            box(*area,
                ui::Layout{.width = ui::pixels(kDot),
                           .height = ui::pixels(kDot),
                           .shrink = 0,
                           .placement = ui::Placement{.absolute = true, .x = ui::pixels(kX), .y = ui::pixels(kY)}},
                kDotFill);
        if (!dot.has_value()) {
            return;
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kScatterShown,
                 "a scatter's copies drawn over its area, its lines as fields",
                 {diagnostics::field("scatter", scene_),
                  diagnostics::field("lines", held_.scatterLines.size()),
                  diagnostics::field("placed", kCopies),
                  diagnostics::field("asked", kScatter->count)});
}

void ShellParticipant::showScatterLines() {
    clear(componentRows_);
    fields_.clear();
    actions_.clear();
    components_ = 0;
    if (auto heading = row(componentsColumn_, "Scatter", kQuiet, kPanel); heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    // Each line but the first a field by its word, holding what follows it;
    // an empty field takes the line away, as a setting's does (D818).
    for (std::size_t at = 1; at < held_.scatterLines.size(); ++at) {
        const std::string kWord = wordOf(held_.scatterLines[at]);
        if (kWord.empty()) {
            continue;
        }
        auto line = fieldRow(kWord, restOfLine(held_.scatterLines[at]));
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(FieldRow{
            .value = line->second, .role = FieldRow::Role::SettingLine, .field = std::to_string(at), .kind = kWord});
    }
    ++components_;
}

void ShellParticipant::reseedScatter() {
    // The next seed, as the session's lines.set of the seed line, or one put
    // in after the first.
    const auto kSeed = std::ranges::find_if(held_.scatterLines, [](const std::string& line) {
        return wordOf(line) == "seed";
    });
    const std::uint64_t kNext =
        kSeed == held_.scatterLines.end() ? 1 : std::strtoull(restOfLine(*kSeed).c_str(), nullptr, 10) + 1;
    Value operation = Value::object();
    operation.add("operation", Value::string(kSeed == held_.scatterLines.end() ? "lines.insert" : "lines.set"));
    operation.add("line", Value::integer(kSeed == held_.scatterLines.end() ? 1 : kSeed - held_.scatterLines.begin()));
    operation.add("text", Value::string("seed " + std::to_string(kNext)));
    commit(std::move(operation), "reseeded to " + std::to_string(kNext), std::nullopt);
}

} // namespace rawframe::studio
