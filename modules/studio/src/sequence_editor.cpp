#include "rawframe/sequence/sequence.h"
#include "shell.h"

#include <algorithm>

namespace rawframe::studio {

namespace {

/// The timeline's width in pixels, a track's row, and a key's mark.
constexpr float kTimelineWidth = 560;
constexpr float kTrackHeight = 22;
constexpr float kMark = 8;
constexpr std::uint32_t kPlayFill = 0x9CC98AFFU;
constexpr std::uint32_t kEventFill = 0xE0B060FFU;

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

} // namespace

void ShellParticipant::showSequence() {
    // A sequence is its lines (D869): each a field of the Inspector, its
    // tracks drawn in the center as rows with their play keys along them,
    // its event keys on a row of their own, and a row that adds a track.
    const std::optional<Value> kRead = read(scene_, "lines.read");
    const Value* kLines = kRead.has_value() ? kRead->find("lines") : nullptr;
    held_.sequenceLines.clear();
    std::string text;
    if (kLines != nullptr && kLines->kind() == Value::Kind::Array) {
        for (const Value& kLine : kLines->items()) {
            held_.sequenceLines.push_back(kLine.text() != nullptr ? *kLine.text() : std::string{});
            text += held_.sequenceLines.back() + "\n";
        }
    }
    if (auto added = row(entitiesColumn_, "Add track", kText, kRow, true); added.has_value()) {
        entityRows_.push_back(*added);
        entities_.push_back("track");
        names_.push_back("Add track");
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
    showSequenceLines();
    clear(viewRows_);
    viewFields_.clear();
    const auto kSequence = sequence::parseSequence(text);
    if (!kSequence.has_value()) {
        return;
    }
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Sequence"));
    // A row for each track, by its name, then one for the events; each key
    // a mark where its time falls along the duration.
    const double kScale = kTimelineWidth / kSequence->duration;
    std::vector<ui::Node> lanes;
    for (std::size_t lane = 0; lane <= kSequence->tracks.size(); ++lane) {
        const bool kEvents = lane == kSequence->tracks.size();
        auto named = row(*pane, kEvents ? "events" : kSequence->tracks[lane], kQuiet, kBackground);
        auto along = box(
            *pane,
            ui::Layout{.width = ui::pixels(kTimelineWidth + kMark), .height = ui::pixels(kTrackHeight), .shrink = 0},
            kPanel);
        if (!named.has_value() || !along.has_value()) {
            return;
        }
        lanes.push_back(*along);
    }
    std::size_t plays = 0;
    std::size_t events = 0;
    for (const sequence::Key& kKey : kSequence->keys) {
        const bool kEvent = kKey.kind == sequence::KeyKind::Event;
        const ui::Node kLane = lanes[kEvent ? kSequence->tracks.size() : kKey.track];
        const auto kX = static_cast<float>(kKey.time * kScale);
        auto mark =
            box(kLane,
                ui::Layout{.width = ui::pixels(kMark),
                           .height = ui::pixels(kTrackHeight - 6),
                           .shrink = 0,
                           .placement = ui::Placement{.absolute = true, .x = ui::pixels(kX), .y = ui::pixels(3)}},
                kEvent ? kEventFill : kPlayFill);
        if (!mark.has_value()) {
            return;
        }
        (kEvent ? events : plays) += 1;
    }
    emitter_.log(diagnostics::Severity::Info,
                 kSequenceShown,
                 "a sequence's tracks and keys drawn along its duration, its lines as fields",
                 {diagnostics::field("sequence", scene_),
                  diagnostics::field("lines", held_.sequenceLines.size()),
                  diagnostics::field("tracks", kSequence->tracks.size()),
                  diagnostics::field("plays", plays),
                  diagnostics::field("events", events)});
}

void ShellParticipant::showSequenceLines() {
    clear(componentRows_);
    fields_.clear();
    actions_.clear();
    components_ = 0;
    if (auto heading = row(componentsColumn_, "Sequence", kQuiet, kPanel); heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    // Each line but the first a field by its word, holding what follows it;
    // an empty field takes the line away, as a setting's does (D818).
    for (std::size_t at = 1; at < held_.sequenceLines.size(); ++at) {
        const std::string kWord = wordOf(held_.sequenceLines[at]);
        if (kWord.empty()) {
            continue;
        }
        auto line = fieldRow(kWord, restOfLine(held_.sequenceLines[at]));
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(FieldRow{
            .value = line->second, .role = FieldRow::Role::SettingLine, .field = std::to_string(at), .kind = kWord});
    }
    ++components_;
}

void ShellParticipant::addSequenceTrack() {
    // A track of a name of its own, put in after the last track, or after
    // the duration and the loop where it has none, before its keys.
    std::size_t after = 0;
    std::size_t tracks = 0;
    for (std::size_t at = 0; at < held_.sequenceLines.size(); ++at) {
        const std::string kWord = wordOf(held_.sequenceLines[at]);
        if (kWord == "track") {
            ++tracks;
        }
        if (kWord == "sequence" || kWord == "duration" || kWord == "loop" || kWord == "track") {
            after = at;
        }
    }
    std::string name = "track" + std::to_string(tracks + 1);
    while (std::ranges::find(held_.sequenceLines, "track " + name) != held_.sequenceLines.end()) {
        name += "_";
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("lines.insert"));
    operation.add("line", Value::integer(static_cast<std::int64_t>(after + 1)));
    operation.add("text", Value::string("track " + name));
    commit(std::move(operation), "added track " + name, std::nullopt);
}

} // namespace rawframe::studio
