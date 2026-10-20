#include "rawframe/authoring_session/game.h"
#include "rawframe/sequence/sequence.h"
#include "shell.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace rawframe::studio {

namespace {

/// The timeline as the design's (D931): the tracks' names in a column,
/// their lanes beside it along the sequence's length, a ruler above.
constexpr float kNamesWide = 200;
constexpr float kLaneTall = 40;
constexpr float kRulerTall = 26;
constexpr float kLeastLanes = 360;
constexpr std::uint32_t kPlayFill = 0x2E2A4FFFU;
constexpr std::uint32_t kPlayEdge = 0x6E62C9FFU;
constexpr std::uint32_t kEventFill = 0xE0B060FFU;
constexpr std::uint32_t kPlayhead = 0xF0505AFFU;

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

/// Seconds as the timeline says them: two places, then " s".
std::string secondsOf(double seconds) {
    std::array<char, 32> made{};
    std::snprintf(made.data(), made.size(), "%.2f s", seconds);
    return made.data();
}

/// A time as a key's line writes it: as short as it is exact to a
/// thousandth.
std::string timeWord(double seconds) {
    std::array<char, 32> made{};
    std::snprintf(made.data(), made.size(), "%.3f", seconds);
    std::string word = made.data();
    while (word.ends_with('0')) {
        word.pop_back();
    }
    if (word.ends_with('.')) {
        word.pop_back();
    }
    return word;
}

std::string hexOf(std::uint64_t value) {
    std::array<char, 17> made{};
    std::snprintf(made.data(), made.size(), "%016llx", static_cast<unsigned long long>(value));
    return made.data();
}

} // namespace

std::vector<std::pair<std::uint64_t, std::string>> ShellParticipant::sequenceAnimators() const {
    // The game's animators, as its description's `animator` lines name
    // them: each identity and its document's name without its kind.
    std::vector<std::pair<std::uint64_t, std::string>> made;
    const std::string kDescribed = authoring_session::readFile(description_).value_or(std::string{});
    std::size_t at = 0;
    while (at < kDescribed.size()) {
        std::size_t end = kDescribed.find('\n', at);
        end = end == std::string::npos ? kDescribed.size() : end;
        const std::string kLine = kDescribed.substr(at, end - at);
        at = end + 1;
        if (wordOf(kLine) != "animator") {
            continue;
        }
        const std::string kRest = restOfLine(kLine);
        const std::size_t kGap = kRest.find(' ');
        if (kGap == std::string::npos) {
            continue;
        }
        const std::uint64_t kId = std::strtoull(kRest.substr(0, kGap).c_str(), nullptr, 16);
        std::string path = kRest.substr(kGap + 1);
        path = path.substr(0, path.find(' '));
        made.emplace_back(kId, std::filesystem::path{path}.stem().string());
    }
    return made;
}

std::string ShellParticipant::animatorName(std::uint64_t animator) const {
    if (animator == 0) {
        return "stop";
    }
    for (const auto& [kId, kName] : sequenceAnimators()) {
        if (kId == animator) {
            return kName;
        }
    }
    return hexOf(animator);
}

ui::Layout ShellParticipant::playheadAt(double time) const {
    return ui::Layout{.width = ui::pixels(2),
                      .height = ui::pixels(held_.sequenceHeadTall),
                      .shrink = 0,
                      .placement = {.absolute = true,
                                    .x = ui::pixels(kNamesWide + (static_cast<float>(time) * held_.sequenceScale)),
                                    .y = ui::pixels(0)}};
}

void ShellParticipant::showSequence() {
    // A sequence is its lines (D869), drawn as the design's timeline
    // (D931): its tracks in the Hierarchy, their keys along lanes under a
    // ruler with the playhead, the transport over them, and the key chosen
    // and the sequence's length in the Inspector.
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
    // Drawn again whole, as a press in it redraws it: the Hierarchy's
    // rows too.
    clear(entityRows_);
    entities_.clear();
    names_.clear();
    brought_.clear();
    removed_.clear();
    places_.clear();
    clear(viewRows_);
    viewFields_.clear();
    clear(held_.cameraRows);
    held_.sequenceKeys.clear();
    held_.sequenceHeads.clear();
    const auto kSequence = sequence::parseSequence(text);
    const auto kRowFor = [this](std::string_view name, std::string_view entity, Icon which, std::uint32_t ink) {
        if (auto made = entityRow(entitiesColumn_, name, 0, false, which, ink, look::kRowInk); made.has_value()) {
            entityRows_.push_back(*made);
            entities_.push_back(std::string{entity});
            names_.push_back(std::string{name});
            brought_.push_back(false);
            removed_.push_back(false);
            places_.push_back(std::nullopt);
        }
    };
    if (kSequence.has_value()) {
        for (const std::string& kTrack : kSequence->tracks) {
            kRowFor(kTrack, "track:" + kTrack, Icon::Cube, 0x8FB8FFFFU);
        }
        kRowFor("Events", "events", Icon::Bookmark, kEventFill);
    }
    if (auto added = row(entitiesColumn_, "Add track", kText, kRow, true); added.has_value()) {
        entityRows_.push_back(*added);
        entities_.push_back("track");
        names_.push_back("Add track");
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
    if (!kSequence.has_value()) {
        if (auto broken = row(centerColumn_,
                              "This sequence does not read: " + std::string{kSequence.error().description()},
                              kWarning,
                              kBackground);
            broken.has_value()) {
            viewRows_.push_back(*broken);
        }
        showSequenceInspector();
        return;
    }
    const double kLength = kSequence->duration;
    held_.sequenceTime = std::clamp(held_.sequenceTime, 0.0, kLength);
    if (held_.sequenceKey.has_value() && *held_.sequenceKey >= kSequence->keys.size()) {
        held_.sequenceKey.reset();
    }
    auto pane = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0),
                               .minHeight = ui::pixels(240),
                               .direction = ui::Direction::Column,
                               .grow = 1,
                               .shrink = 0},
                    look::kPanelFill);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Sequence"));
    // The transport: to the start, play, where the playhead is of the
    // length, the keys added there, the snap, and whether it loops.
    if (auto bar = box(*pane,
                       ui::Layout{.height = ui::pixels(40),
                                  .alignItems = ui::Align::Center,
                                  .gap = 8,
                                  .shrink = 0,
                                  .padding = {12, 12, 0, 0},
                                  .border = {0, 0, 0, 1}},
                       0);
        bar.has_value()) {
        static_cast<void>(tree_->setLook(*bar, ui::Look{.borderColor = look::kLine}));
        held_.sequenceStart = iconButton(*bar, Icon::Undo, "To the start").value_or(ui::Node{});
        held_.sequencePlay =
            iconButton(
                *bar, held_.sequencePlaying ? Icon::Pause : Icon::Play, "Play the sequence", held_.sequencePlaying)
                .value_or(ui::Node{});
        if (auto now = box(*bar, ui::Layout{.shrink = 0, .padding = {6, 0, 0, 0}}, 0); now.has_value()) {
            held_.sequenceNow = *now;
            static_cast<void>(monoWords(*now, secondsOf(held_.sequenceTime), look::kInk, look::kBodySize));
        }
        if (auto of = box(*bar, ui::Layout{.shrink = 0}, 0); of.has_value()) {
            static_cast<void>(words(*of, "of " + secondsOf(kLength), look::kFaintInk, look::kLabelSize));
        }
        held_.sequenceAddKey = textButton(*bar, Icon::Plus, "Key").value_or(ui::Node{});
        held_.sequenceAddEvent = textButton(*bar, Icon::Plus, "Event").value_or(ui::Node{});
        static_cast<void>(box(*bar, ui::Layout{.grow = 1}, 0));
        if (auto snap = box(*bar, ui::Layout{.shrink = 0}, 0); snap.has_value()) {
            static_cast<void>(
                words(*snap, "Snap " + timeWord(held_.sequenceSnap) + " s", look::kSoftInk, look::kLabelSize));
        }
        held_.sequenceLoop = textButton(*bar,
                                        kSequence->loop ? std::optional<Icon>{Icon::Check} : std::nullopt,
                                        kSequence->loop ? "Loops" : "Loop")
                                 .value_or(ui::Node{});
    }
    // How many pixels a second is, the lanes filling the center.
    const float kCenterWide = tree_->placedRectOf(centerColumn_).width;
    const float kLanesWide = std::max(kLeastLanes, kCenterWide - kNamesWide - 24);
    held_.sequenceScale = kLanesWide / static_cast<float>(kLength);
    held_.sequenceLength = kLength;
    auto body = box(*pane,
                    ui::Layout{.height = ui::pixels(0),
                               .minHeight = ui::pixels(0),
                               .direction = ui::Direction::Column,
                               .grow = 1,
                               .scroll = ui::Scroll::Vertical},
                    0);
    if (!body.has_value()) {
        return;
    }
    // The ruler: a mark each second, or each tenth where the sequence is
    // short, the playhead's head on it.
    const auto kLine = [this](ui::Node parent,
                              std::string_view name,
                              std::string_view kind,
                              Icon which,
                              std::uint32_t ink) -> std::optional<ui::Node> {
        auto line = box(parent, ui::Layout{.height = ui::pixels(kLaneTall), .shrink = 0, .border = {0, 0, 0, 1}}, 0);
        if (!line.has_value()) {
            return std::nullopt;
        }
        static_cast<void>(tree_->setLook(*line, ui::Look{.borderColor = 0x24262CFFU}));
        if (auto named = box(*line,
                             ui::Layout{.width = ui::pixels(kNamesWide),
                                        .alignItems = ui::Align::Center,
                                        .gap = 6,
                                        .shrink = 0,
                                        .padding = {12, 8, 0, 0},
                                        .border = {0, 1, 0, 0}},
                             0);
            named.has_value()) {
            static_cast<void>(tree_->setLook(*named, ui::Look{.borderColor = look::kLine}));
            static_cast<void>(icon(*named, which, ink, 13));
            if (auto said = box(*named, ui::Layout{.shrink = 0}, 0); said.has_value()) {
                static_cast<void>(words(*said, name, look::kInk, look::kBodySize));
            }
            if (auto quiet = box(*named, ui::Layout{.shrink = 0}, 0); quiet.has_value()) {
                static_cast<void>(words(*quiet, kind, look::kFaintInk, look::kLabelSize));
            }
        }
        return box(*line, ui::Layout{.width = ui::pixels(0), .grow = 1, .shrink = 0}, 0).value_or(ui::Node{});
    };
    if (auto ruler = box(*body, ui::Layout{.height = ui::pixels(kRulerTall), .shrink = 0, .border = {0, 0, 0, 1}}, 0);
        ruler.has_value()) {
        static_cast<void>(tree_->setLook(*ruler, ui::Look{.borderColor = look::kLine}));
        static_cast<void>(box(*ruler, ui::Layout{.width = ui::pixels(kNamesWide), .shrink = 0}, 0));
        if (auto marks = box(*ruler, ui::Layout{.width = ui::pixels(kLanesWide), .shrink = 0}, 0); marks.has_value()) {
            held_.sequenceRuler = *marks;
            static_cast<void>(tree_->setName(*marks, "Sequence ruler"));
            const double kStep = kLength <= 2 ? 0.25 : (kLength <= 12 ? 1 : std::ceil(kLength / 12));
            for (double at = 0; at <= kLength + 1e-9; at += kStep) {
                const auto kX = static_cast<float>(at) * held_.sequenceScale;
                if (auto tick =
                        box(*marks,
                            ui::Layout{.width = ui::pixels(1),
                                       .height = ui::pixels(kRulerTall),
                                       .shrink = 0,
                                       .placement = {.absolute = true, .x = ui::pixels(kX), .y = ui::pixels(0)}},
                            look::kLine);
                    tick.has_value()) {
                    static_cast<void>(
                        tree_->setInteraction(*tick, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
                if (auto label =
                        box(*marks,
                            ui::Layout{.shrink = 0,
                                       .placement = {.absolute = true, .x = ui::pixels(kX + 4), .y = ui::pixels(6)}},
                            0);
                    label.has_value()) {
                    static_cast<void>(monoWords(*label, timeWord(at) + "s", look::kFaintInk, look::kSmallSize));
                    static_cast<void>(
                        tree_->setInteraction(*label, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
            }
        }
    }
    // A lane for each track, then the events'.
    const std::vector<std::pair<std::uint64_t, std::string>> kAnimators = sequenceAnimators();
    std::vector<ui::Node> lanes;
    for (const std::string& kTrack : kSequence->tracks) {
        lanes.push_back(kLine(*body, kTrack, "Animator", Icon::Cube, 0x8FB8FFFFU).value_or(ui::Node{}));
    }
    lanes.push_back(kLine(*body, "Events", "Kest", Icon::Bookmark, kEventFill).value_or(ui::Node{}));
    std::size_t plays = 0;
    std::size_t events = 0;
    for (std::size_t each = 0; each < kSequence->keys.size(); ++each) {
        const sequence::Key& kKey = kSequence->keys[each];
        const bool kEvent = kKey.kind == sequence::KeyKind::Event;
        const ui::Node kLane = lanes[kEvent ? kSequence->tracks.size() : kKey.track];
        const bool kKeyChosen = held_.sequenceKey == each;
        const auto kX = static_cast<float>(kKey.time) * held_.sequenceScale;
        std::optional<ui::Node> mark;
        if (kEvent) {
            // A diamond where it fires, its name beside it.
            const auto kMadeMark =
                box(kLane,
                    ui::Layout{.height = ui::pixels(22),
                               .alignItems = ui::Align::Center,
                               .gap = 6,
                               .shrink = 0,
                               .placement = {.absolute = true, .x = ui::pixels(kX - 7), .y = ui::pixels(9)}},
                    0);
            if (kMadeMark.has_value()) {
                mark = *kMadeMark;
            }
            if (mark.has_value()) {
                if (auto diamond = box(*mark,
                                       ui::Layout{.width = ui::pixels(12), .height = ui::pixels(12), .shrink = 0},
                                       kKeyChosen ? look::kAccent : kEventFill);
                    diamond.has_value()) {
                    static_cast<void>(tree_->setLook(
                        *diamond, ui::Look{.fill = kKeyChosen ? look::kAccent : kEventFill, .radius = 3}));
                    static_cast<void>(
                        tree_->setInteraction(*diamond, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
                if (auto named = box(*mark, ui::Layout{.shrink = 0}, 0); named.has_value()) {
                    static_cast<void>(monoWords(*named, kKey.name, look::kSoftInk, look::kLabelSize));
                    static_cast<void>(
                        tree_->setInteraction(*named, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
            }
            ++events;
        } else {
            // A bar from its time to the next play on its track, or the end:
            // the animator it starts plays there.
            double until = kLength;
            for (std::size_t later = each + 1; later < kSequence->keys.size(); ++later) {
                if (kSequence->keys[later].kind == sequence::KeyKind::Play &&
                    kSequence->keys[later].track == kKey.track) {
                    until = kSequence->keys[later].time;
                    break;
                }
            }
            const float kWide = std::max(10.0F, static_cast<float>(until - kKey.time) * held_.sequenceScale - 2);
            const auto kMadeMark =
                box(kLane,
                    ui::Layout{.width = ui::pixels(kWide),
                               .height = ui::pixels(24),
                               .alignItems = ui::Align::Center,
                               .shrink = 0,
                               .padding = {8, 6, 0, 0},
                               .border = {1, 1, 1, 1},
                               .placement = {.absolute = true, .x = ui::pixels(kX), .y = ui::pixels(8)}},
                    0);
            if (kMadeMark.has_value()) {
                mark = *kMadeMark;
            }
            if (mark.has_value()) {
                static_cast<void>(tree_->setLook(*mark,
                                                 ui::Look{.fill = kKey.animator == 0 ? 0x2A2C33FFU : kPlayFill,
                                                          .borderColor = kKeyChosen ? look::kAccent : kPlayEdge,
                                                          .radius = 4,
                                                          .clip = true}));
                if (auto named = box(*mark, ui::Layout{.shrink = 0}, 0); named.has_value()) {
                    static_cast<void>(words(*named, animatorName(kKey.animator), look::kInk, look::kLabelSize));
                    static_cast<void>(
                        tree_->setInteraction(*named, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
            }
            ++plays;
        }
        if (mark.has_value()) {
            static_cast<void>(tree_->setRole(*mark, ui::Role::Button));
            static_cast<void>(tree_->setName(
                *mark, (kEvent ? kKey.name : animatorName(kKey.animator)) + " at " + timeWord(kKey.time)));
            held_.sequenceKeys.emplace_back(*mark, each);
        }
    }
    // The playhead, over the ruler and the lanes.
    held_.sequenceHeadTall = kRulerTall + (kLaneTall * static_cast<float>(lanes.size()));
    if (auto head = box(*body, playheadAt(held_.sequenceTime), kPlayhead); head.has_value()) {
        static_cast<void>(tree_->setInteraction(*head, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        held_.sequenceHeads.push_back(*head);
    }
    showSequenceInspector();
    emitter_.log(diagnostics::Severity::Info,
                 kSequenceShown,
                 "a sequence's tracks and keys drawn along its duration, its lines as fields",
                 {diagnostics::field("sequence", scene_),
                  diagnostics::field("lines", held_.sequenceLines.size()),
                  diagnostics::field("tracks", kSequence->tracks.size()),
                  diagnostics::field("plays", plays),
                  diagnostics::field("events", events),
                  diagnostics::field("animators", kAnimators.size())});
}

void ShellParticipant::showSequenceLines() {
    showSequenceInspector();
}

void ShellParticipant::showSequenceInspector() {
    clear(componentRows_);
    fields_.clear();
    actions_.clear();
    components_ = 0;
    held_.sequenceRemove = ui::Node{};
    held_.sequenceText = ui::Node{};
    std::string text;
    for (const std::string& kLine : held_.sequenceLines) {
        text += kLine + "\n";
    }
    const auto kSequence = sequence::parseSequence(text);
    const auto kSection = [this](std::string_view title, std::string_view quiet) {
        auto heading = box(componentsColumn_,
                           ui::Layout{.alignItems = ui::Align::Center,
                                      .gap = 8,
                                      .shrink = 0,
                                      .padding = {6, 4, 8, 6},
                                      .margin = {0, 0, 6, 0},
                                      .border = {0, 0, 1, 0}},
                           0);
        if (!heading.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(*heading, ui::Look{.borderColor = 0x24262CFFU}));
        componentRows_.push_back(*heading);
        static_cast<void>(icon(*heading, Icon::ChevronDown, look::kFaintInk, 12));
        if (auto named = box(*heading, ui::Layout{.shrink = 0}, 0); named.has_value()) {
            static_cast<void>(strongWords(*named, title, look::kInk));
        }
        if (auto owner = box(*heading, ui::Layout{.shrink = 1}, 0); owner.has_value()) {
            static_cast<void>(monoWords(*owner, quiet, look::kFaintInk, look::kSmallSize));
        }
    };
    const auto kFieldOf = [this](std::string_view name, std::string_view value, std::string_view shown, FieldRow made) {
        if (auto line = fieldRow(name, value, std::nullopt, true, shown); line.has_value()) {
            componentRows_.push_back(line->first);
            made.value = line->second;
            fields_.push_back(std::move(made));
        }
    };
    // The key chosen: its head, then its time and what it does.
    if (kSequence.has_value() && held_.sequenceKey.has_value() && *held_.sequenceKey < kSequence->keys.size()) {
        const sequence::Key& kKey = kSequence->keys[*held_.sequenceKey];
        const bool kEvent = kKey.kind == sequence::KeyKind::Event;
        if (auto head =
                box(componentsColumn_,
                    ui::Layout{.direction = ui::Direction::Column, .gap = 3, .shrink = 0, .padding = {8, 8, 10, 10}},
                    0);
            head.has_value()) {
            componentRows_.push_back(*head);
            if (auto title = box(*head, ui::Layout{.shrink = 0}, 0); title.has_value()) {
                static_cast<void>(
                    strongWords(*title, kEvent ? kKey.name : animatorName(kKey.animator), look::kInk, 15));
            }
            if (auto quiet = box(*head, ui::Layout{.shrink = 0}, 0); quiet.has_value()) {
                static_cast<void>(words(
                    *quiet,
                    (kEvent ? std::string{"event key on Events"} : "play key on " + kSequence->tracks[kKey.track]) +
                        " at " + secondsOf(kKey.time),
                    look::kFaintInk,
                    look::kLabelSize));
            }
        }
        kSection("Key", "rawframe.sequence");
        const std::string kAt = std::to_string(*held_.sequenceKey);
        kFieldOf("key time",
                 timeWord(kKey.time),
                 "Time",
                 FieldRow{.role = FieldRow::Role::SequenceKey, .field = kAt, .kind = "time"});
        if (kEvent) {
            kFieldOf("key event",
                     kKey.name,
                     "Event",
                     FieldRow{.role = FieldRow::Role::SequenceKey, .field = kAt, .kind = "name"});
        } else {
            kFieldOf("key track",
                     kSequence->tracks[kKey.track],
                     "Track",
                     FieldRow{.role = FieldRow::Role::SequenceKey, .field = kAt, .kind = "track"});
            kFieldOf("key animator",
                     animatorName(kKey.animator),
                     "Animator",
                     FieldRow{.role = FieldRow::Role::SequenceKey, .field = kAt, .kind = "animator"});
            std::string offered;
            for (const auto& [kId, kName] : sequenceAnimators()) {
                offered += (offered.empty() ? "" : ", ") + kName;
            }
            if (auto said = row(componentsColumn_,
                                offered.empty() ? std::string{"The game declares no animator; stop ends a track's"}
                                                : "The game's animators: " + offered + ", or stop",
                                kQuiet,
                                0U);
                said.has_value()) {
                componentRows_.push_back(*said);
            }
        }
        if (auto remove = textButton(componentsColumn_, Icon::Trash, "Delete key"); remove.has_value()) {
            componentRows_.push_back(*remove);
            held_.sequenceRemove = *remove;
        }
    }
    // The sequence: its length, a line of the document as typed, and the
    // document opened as text.
    kSection("Sequence", scene_);
    for (std::size_t at = 1; at < held_.sequenceLines.size(); ++at) {
        if (wordOf(held_.sequenceLines[at]) == "duration") {
            kFieldOf("duration",
                     restOfLine(held_.sequenceLines[at]),
                     "Length",
                     FieldRow{.role = FieldRow::Role::SettingLine, .field = std::to_string(at), .kind = "duration"});
        }
    }
    if (kSequence.has_value()) {
        if (auto said =
                row(componentsColumn_,
                    std::to_string(kSequence->tracks.size()) + " tracks, " + std::to_string(kSequence->keys.size()) +
                        " keys" + (kSequence->loop ? ", starts again at its end" : ", stops at its end"),
                    kQuiet,
                    0U);
            said.has_value()) {
            componentRows_.push_back(*said);
        }
    }
    if (auto edit = textButton(componentsColumn_, Icon::File, "Edit as text"); edit.has_value()) {
        componentRows_.push_back(*edit);
        held_.sequenceText = *edit;
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

std::optional<std::size_t> ShellParticipant::sequenceKeyLine(std::size_t key) const {
    // The keys are the document's play and event lines, in order.
    std::size_t seen = 0;
    for (std::size_t at = 0; at < held_.sequenceLines.size(); ++at) {
        const std::string kWord = wordOf(held_.sequenceLines[at]);
        if (kWord == "play" || kWord == "event") {
            if (seen == key) {
                return at;
            }
            ++seen;
        }
    }
    return std::nullopt;
}

void ShellParticipant::putSequenceKey(std::optional<std::size_t> replaced,
                                      double time,
                                      const std::string& line,
                                      const std::string& done) {
    // Keys stay in time order: the old line out, the new one in after the
    // last key at or before its time, as one step of the history.
    std::vector<Value> operations;
    std::vector<std::string> lines = held_.sequenceLines;
    const std::optional<std::size_t> kOld = replaced.has_value() ? sequenceKeyLine(*replaced) : std::nullopt;
    if (kOld.has_value()) {
        Value out = Value::object();
        out.add("operation", Value::string("lines.set"));
        out.add("line", Value::integer(static_cast<std::int64_t>(*kOld)));
        out.add("text", Value{});
        operations.push_back(std::move(out));
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*kOld));
    }
    std::size_t into = lines.size();
    for (std::size_t at = 0; at < lines.size(); ++at) {
        const std::string kWord = wordOf(lines[at]);
        if (kWord != "play" && kWord != "event") {
            continue;
        }
        const double kTime = std::strtod(restOfLine(lines[at]).c_str(), nullptr);
        if (kTime > time + 1e-9) {
            into = at;
            break;
        }
    }
    Value in = Value::object();
    in.add("operation", Value::string("lines.insert"));
    in.add("line", Value::integer(static_cast<std::int64_t>(into)));
    in.add("text", Value::string(line));
    operations.push_back(std::move(in));
    // The key keeps being chosen where it lands.
    std::size_t keysBefore = 0;
    for (std::size_t at = 0; at < into; ++at) {
        const std::string kWord = wordOf(lines[at]);
        keysBefore += kWord == "play" || kWord == "event" ? 1 : 0;
    }
    held_.sequenceKey = keysBefore;
    commitAll(std::move(operations), done, std::nullopt);
}

void ShellParticipant::setSequenceKey(const FieldRow& row, const std::string& typed) {
    std::string text;
    for (const std::string& kLine : held_.sequenceLines) {
        text += kLine + "\n";
    }
    const auto kSequence = sequence::parseSequence(text);
    const std::size_t kAt = std::strtoull(row.field.c_str(), nullptr, 10);
    if (!kSequence.has_value() || kAt >= kSequence->keys.size()) {
        return;
    }
    sequence::Key key = kSequence->keys[kAt];
    if (row.kind == "time") {
        char* end = nullptr;
        const double kTime = std::strtod(typed.c_str(), &end);
        if (end == typed.c_str() || !std::isfinite(kTime) || kTime < 0 || kTime > kSequence->duration) {
            refuse("a key's time is from 0 to the sequence's length, " + secondsOf(kSequence->duration));
            return;
        }
        key.time = kTime;
    } else if (row.kind == "name") {
        if (typed.empty() || typed.find(' ') != std::string::npos) {
            refuse("an event's name is one machine name, as the game's Kest reads it");
            return;
        }
        key.name = typed;
    } else if (row.kind == "track") {
        const auto kTrack = std::ranges::find(kSequence->tracks, typed);
        if (kTrack == kSequence->tracks.end()) {
            refuse("a key's track is one of the sequence's tracks");
            return;
        }
        key.track = static_cast<std::uint32_t>(kTrack - kSequence->tracks.begin());
    } else if (row.kind == "animator") {
        std::optional<std::uint64_t> found = typed == "stop" ? std::optional<std::uint64_t>{0} : std::nullopt;
        for (const auto& [kId, kName] : sequenceAnimators()) {
            if (kName == typed) {
                found = kId;
            }
        }
        if (!found.has_value()) {
            refuse("a key starts one of the game's animators, by its name, or stop");
            return;
        }
        key.animator = *found;
    }
    const std::string kLine =
        key.kind == sequence::KeyKind::Event
            ? "event " + timeWord(key.time) + " " + hexOf(key.event) + " " + key.name
            : "play " + timeWord(key.time) + " " + kSequence->tracks[key.track] + " " + hexOf(key.animator);
    putSequenceKey(kAt, key.time, kLine, "key " + row.kind + " set to " + typed);
}

bool ShellParticipant::pressSequence(ui::Node node) {
    if (!chosen(Listed::Sequence) || node == ui::Node{}) {
        return false;
    }
    std::string text;
    for (const std::string& kLine : held_.sequenceLines) {
        text += kLine + "\n";
    }
    const auto kSequence = sequence::parseSequence(text);
    if (const auto kKey = std::ranges::find(held_.sequenceKeys, node, &std::pair<ui::Node, std::size_t>::first);
        kKey != held_.sequenceKeys.end()) {
        held_.sequenceKey = kKey->second;
        if (kSequence.has_value() && kKey->second < kSequence->keys.size()) {
            held_.sequenceTime = kSequence->keys[kKey->second].time;
        }
        showSequence();
        say("key at " + secondsOf(held_.sequenceTime) + " chosen");
        return true;
    }
    if (node == held_.sequenceRuler) {
        // The playhead where the ruler was pressed, on the snap.
        const ui::Rect kAt = tree_->placedRectOf(held_.sequenceRuler);
        const double kTime = (pressedAt_[0] - kAt.x) / std::max(1.0F, held_.sequenceScale);
        held_.sequenceTime =
            std::clamp(std::round(kTime / held_.sequenceSnap) * held_.sequenceSnap, 0.0, held_.sequenceLength);
        showSequence();
        say("playhead at " + secondsOf(held_.sequenceTime));
        return true;
    }
    if (node == held_.sequenceStart) {
        held_.sequenceTime = 0;
        showSequence();
        return true;
    }
    if (node == held_.sequencePlay) {
        held_.sequencePlaying = !held_.sequencePlaying;
        if (held_.sequencePlaying && held_.sequenceTime >= held_.sequenceLength) {
            held_.sequenceTime = 0;
        }
        held_.sequenceRan = std::nullopt;
        showSequence();
        say(held_.sequencePlaying ? "playing the sequence" : "sequence paused at " + secondsOf(held_.sequenceTime));
        return true;
    }
    if (node == held_.sequenceText) {
        openEditor(Diagnostic{.file = "game/" + scene_, .line = 1, .column = 1});
        return true;
    }
    if (!kSequence.has_value()) {
        return false;
    }
    if (node == held_.sequenceLoop) {
        // The loop line in after the duration, or taken out.
        for (std::size_t at = 0; at < held_.sequenceLines.size(); ++at) {
            if (wordOf(held_.sequenceLines[at]) == "loop") {
                Value operation = Value::object();
                operation.add("operation", Value::string("lines.set"));
                operation.add("line", Value::integer(static_cast<std::int64_t>(at)));
                operation.add("text", Value{});
                commit(std::move(operation), "the sequence stops at its end", std::nullopt);
                return true;
            }
        }
        for (std::size_t at = 0; at < held_.sequenceLines.size(); ++at) {
            if (wordOf(held_.sequenceLines[at]) == "duration") {
                // A loop has no key at its end.
                if (std::ranges::any_of(kSequence->keys, [&](const sequence::Key& key) {
                        return key.time >= kSequence->duration;
                    })) {
                    refuse("a sequence that loops has no key at its end");
                    return true;
                }
                Value operation = Value::object();
                operation.add("operation", Value::string("lines.insert"));
                operation.add("line", Value::integer(static_cast<std::int64_t>(at + 1)));
                operation.add("text", Value::string("loop"));
                commit(std::move(operation), "the sequence starts again at its end", std::nullopt);
                return true;
            }
        }
        return true;
    }
    if (node == held_.sequenceAddKey) {
        if (kSequence->tracks.empty()) {
            refuse("add a track first: a play key starts an animator on a track");
            return true;
        }
        const auto kAnimators = sequenceAnimators();
        const std::uint32_t kTrack = held_.sequenceKey.has_value() && *held_.sequenceKey < kSequence->keys.size() &&
                                             kSequence->keys[*held_.sequenceKey].kind == sequence::KeyKind::Play
                                         ? kSequence->keys[*held_.sequenceKey].track
                                         : 0;
        const std::uint64_t kAnimator = kAnimators.empty() ? 0 : kAnimators.front().first;
        putSequenceKey(std::nullopt,
                       held_.sequenceTime,
                       "play " + timeWord(held_.sequenceTime) + " " + kSequence->tracks[kTrack] + " " +
                           hexOf(kAnimator),
                       "added a key at " + secondsOf(held_.sequenceTime));
        return true;
    }
    if (node == held_.sequenceAddEvent) {
        // An event of a name and an identity of its own.
        std::string name =
            "event" +
            std::to_string(std::ranges::count(kSequence->keys, sequence::KeyKind::Event, &sequence::Key::kind) + 1);
        while (std::ranges::any_of(kSequence->keys, [&](const sequence::Key& key) {
            return key.name == name;
        })) {
            name += "_";
        }
        std::random_device device;
        std::uint64_t identity = 0;
        while (identity == 0 || std::ranges::any_of(kSequence->keys, [&](const sequence::Key& key) {
                   return key.kind == sequence::KeyKind::Event && key.event == identity;
               })) {
            identity = (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
        }
        putSequenceKey(std::nullopt,
                       held_.sequenceTime,
                       "event " + timeWord(held_.sequenceTime) + " " + hexOf(identity) + " " + name,
                       "added event " + name + " at " + secondsOf(held_.sequenceTime));
        return true;
    }
    if (node == held_.sequenceRemove && held_.sequenceKey.has_value()) {
        if (const std::optional<std::size_t> kLine = sequenceKeyLine(*held_.sequenceKey); kLine.has_value()) {
            Value operation = Value::object();
            operation.add("operation", Value::string("lines.set"));
            operation.add("line", Value::integer(static_cast<std::int64_t>(*kLine)));
            operation.add("text", Value{});
            held_.sequenceKey.reset();
            commit(std::move(operation), "key deleted", std::nullopt);
        }
        return true;
    }
    return false;
}

void ShellParticipant::runSequence(double seconds) {
    // The playhead moved on while it plays, as `sequence::advance` plays
    // it, only the head and the time said again.
    if (!held_.sequencePlaying || !chosen(Listed::Sequence)) {
        held_.sequenceRan = std::nullopt;
        return;
    }
    const double kSince = held_.sequenceRan.has_value() ? seconds - *held_.sequenceRan : 0;
    held_.sequenceRan = seconds;
    std::string text;
    for (const std::string& kLine : held_.sequenceLines) {
        text += kLine + "\n";
    }
    const auto kSequence = sequence::parseSequence(text);
    if (!kSequence.has_value()) {
        held_.sequencePlaying = false;
        return;
    }
    const sequence::Advance kOn = sequence::advance(*kSequence, held_.sequenceTime, kSince);
    held_.sequenceTime = kOn.time;
    for (const ui::Node kHead : held_.sequenceHeads) {
        static_cast<void>(tree_->setLayout(kHead, playheadAt(kOn.time)));
    }
    if (tree_->contains(held_.sequenceNow)) {
        static_cast<void>(monoWords(held_.sequenceNow, secondsOf(kOn.time), look::kInk, look::kBodySize));
    }
    if (kOn.ended) {
        held_.sequencePlaying = false;
        showSequence();
        say("the sequence ended at " + secondsOf(kOn.time));
    }
}

} // namespace rawframe::studio
