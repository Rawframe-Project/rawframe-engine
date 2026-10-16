#include "shell.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <span>
#include <tuple>

namespace rawframe::studio {

namespace {

constexpr float kLabelWidth = 170;
constexpr float kPerSecond = 480;
/// What a clip of any length is drawn within: a sheet at most this wide,
/// its ruler at most this many ticks, and at most this many lanes, so a
/// clip that says it lasts a day lays out as one that lasts a second does.
constexpr float kMostWide = 16'384;
constexpr double kMostTicks = 200;
constexpr std::size_t kMostLanes = 256;
constexpr float kRuler = 22;
constexpr float kLane = 24;
constexpr float kMark = 9;
constexpr float kMargin = 16;
constexpr std::uint32_t kLaneFill = 0x26282EFFU;
constexpr std::uint32_t kTick = 0x50545EFFU;

/// `seconds` said short, as `0.5`.
std::string timeText(double seconds) {
    char made[32];
    std::snprintf(made, sizeof made, "%g", seconds);
    return made;
}

const std::string* textOf(const Value& object, std::string_view name) {
    const Value* kFound = object.kind() == Value::Kind::Object ? object.find(name) : nullptr;
    return kFound != nullptr && kFound->kind() == Value::Kind::String ? kFound->text() : nullptr;
}

double timeOf(const Value& item) {
    const Value* kTime = item.kind() == Value::Kind::Object ? item.find("time") : nullptr;
    return kTime != nullptr ? kTime->real().value_or(0) : 0;
}

/// The items of `clip`'s array `name`, or none.
std::span<const Value> itemsOf(const Value& clip, std::string_view name) {
    const Value* kFound = clip.find(name);
    return kFound != nullptr && kFound->kind() == Value::Kind::Array ? kFound->items() : std::span<const Value>{};
}

} // namespace

void ShellParticipant::showTimeline(const Value& clip) {
    clear(viewRows_);
    viewFields_.clear();
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
    const Value* kDuration = clip.find("duration");
    const double kLength = std::max(kDuration != nullptr ? kDuration->real().value_or(0) : 0, 0.01);
    // Each bone a track names by its skeleton's name for it, where the
    // skeleton is a document of the game's: else by the track's place.
    std::map<std::string, std::string, std::less<>> boneNames;
    if (const std::string* kSkeleton = textOf(clip, "skeleton"); kSkeleton != nullptr) {
        const auto kSource = std::ranges::find(browser_.sources, *kSkeleton, &ListedSource::id);
        const std::optional<Value> kRead =
            kSource != browser_.sources.end() ? read(kSource->path, "data.read") : std::nullopt;
        const Value* kData = kRead.has_value() ? kRead->find("data") : nullptr;
        for (const Value& kBone : kData != nullptr ? itemsOf(*kData, "bones") : std::span<const Value>{}) {
            const std::string* kTarget = textOf(kBone, "target");
            const std::string* kName = textOf(kBone, "name");
            if (kTarget != nullptr && kName != nullptr) {
                boneNames[*kTarget] = *kName;
            }
        }
    }
    // A lane for each track, then one for the events and one for the sync
    // markers where the clip holds them; each lane's marks, its items'
    // rows' paths, and its name.
    struct Lane {
        std::string name;
        std::vector<std::pair<double, std::string>> marks;
    };
    std::vector<Lane> lanes;
    std::size_t keys = 0;
    const std::span<const Value> kTracks = itemsOf(clip, "tracks");
    for (std::size_t track = 0; track < kTracks.size() && lanes.size() < kMostLanes; ++track) {
        const std::string* kBone = textOf(kTracks[track], "bone");
        const std::string* kChannel = textOf(kTracks[track], "channel");
        const auto kNamed = kBone != nullptr ? boneNames.find(*kBone) : boneNames.end();
        Lane lane{.name = (kNamed != boneNames.end() ? kNamed->second : "track " + std::to_string(track + 1)) + " " +
                          (kChannel != nullptr ? *kChannel : std::string{}),
                  .marks = {}};
        const std::span<const Value> kKeys = itemsOf(kTracks[track], "keys");
        for (std::size_t key = 0; key < kKeys.size(); ++key) {
            lane.marks.emplace_back(timeOf(kKeys[key]),
                                    "tracks[" + std::to_string(track) + "].keys[" + std::to_string(key) + "]");
        }
        keys += kKeys.size();
        lanes.push_back(std::move(lane));
    }
    std::size_t events = 0;
    std::size_t markers = 0;
    for (const auto& [kMember, kLaneName, kCount] :
         {std::tuple{"events", "events", &events}, std::tuple{"syncMarkers", "sync markers", &markers}}) {
        const std::span<const Value> kItems = itemsOf(clip, kMember);
        if (kItems.empty() || lanes.size() >= kMostLanes) {
            continue;
        }
        Lane lane{.name = kLaneName, .marks = {}};
        for (std::size_t each = 0; each < kItems.size(); ++each) {
            lane.marks.emplace_back(timeOf(kItems[each]), std::string{kMember} + "[" + std::to_string(each) + "]");
        }
        *kCount = kItems.size();
        lanes.push_back(std::move(lane));
    }
    // A pane scrolling both ways over a sheet as long as the clip, its
    // ruler on top and its lanes under it.
    const float kPerSecondShown =
        std::min(kPerSecond, (kMostWide - kLabelWidth - 2 * kMargin) / static_cast<float>(kLength));
    const float kWide = kLabelWidth + static_cast<float>(kLength) * kPerSecondShown + 2 * kMargin;
    const float kTall = kRuler + kLane * static_cast<float>(lanes.size()) + kMargin;
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Timeline"));
    auto sheet =
        box(*pane, ui::Layout{.width = ui::pixels(kWide), .height = ui::pixels(kTall), .shrink = 0}, kBackground);
    if (!sheet.has_value()) {
        return;
    }
    const auto kPlace = [](float x, float y, float width, float height) {
        return ui::Layout{.width = ui::pixels(width),
                          .height = ui::pixels(height),
                          .shrink = 0,
                          .placement = ui::Placement{.absolute = true, .x = ui::pixels(x), .y = ui::pixels(y)}};
    };
    const auto kAt = [kPerSecondShown](double seconds) {
        return kLabelWidth + static_cast<float>(seconds) * kPerSecondShown;
    };
    // A tick each tenth of a second, a quarter, a half, a second, or more,
    // the least that leaves each tick's words room and the ruler its most
    // ticks; each said.
    double step = kLength / kMostTicks;
    for (const double kEach : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 300.0, 600.0, 3600.0}) {
        if (kEach * static_cast<double>(kPerSecondShown) >= 48 && kLength / kEach <= kMostTicks) {
            step = kEach;
            break;
        }
    }
    const double kStep = std::max(step, kLength / kMostTicks);
    for (std::size_t tick = 0;
         tick <= static_cast<std::size_t>(kMostTicks) && static_cast<double>(tick) * kStep <= kLength + 1e-9;
         ++tick) {
        const double kSeconds = static_cast<double>(tick) * kStep;
        static_cast<void>(box(*sheet, kPlace(kAt(kSeconds), 0, 1, kTall), kTick));
        auto said = box(*sheet, kPlace(kAt(kSeconds) + 3, 2, 44, kRuler - 4), 0);
        if (said.has_value()) {
            static_cast<void>(words(*said, timeText(kSeconds), kQuiet, 11));
        }
    }
    for (std::size_t each = 0; each < lanes.size(); ++each) {
        const float kY = kRuler + kLane * static_cast<float>(each);
        static_cast<void>(
            box(*sheet, kPlace(kAt(0), kY + 2, static_cast<float>(kLength) * kPerSecondShown, kLane - 4), kLaneFill));
        auto label = box(*sheet, kPlace(0, kY, kLabelWidth - 6, kLane), 0);
        if (label.has_value()) {
            static_cast<void>(words(*label, lanes[each].name, kText, 12));
        }
        // Each key a mark at its time, named by its lane and its time, as
        // `hips translation at 0.5`; pressed, it is its row pressed.
        for (const auto& [kSeconds, kPath] : lanes[each].marks) {
            const auto kListed = std::ranges::find(entities_, kPath);
            if (kListed == entities_.end()) {
                continue;
            }
            const std::size_t kEntity = static_cast<std::size_t>(kListed - entities_.begin());
            auto mark = box(*sheet,
                            kPlace(kAt(kSeconds) - kMark / 2, kY + (kLane - 2 * kMark) / 2, kMark, 2 * kMark),
                            kPath == entity_ ? kChosen : kRow);
            if (!mark.has_value() || !tree_->setRole(*mark, ui::Role::Button).has_value() ||
                !tree_->setName(*mark, lanes[each].name + " at " + timeText(kSeconds)).has_value()) {
                return;
            }
            held_.canvasBoxes.emplace_back(*mark, kEntity);
            held_.canvasParts.emplace_back(*mark, kEntity);
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kTimelineShown,
                 "a clip's tracks, events, and markers laid out over its time",
                 {diagnostics::field("tracks", kTracks.size()),
                  diagnostics::field("keys", keys),
                  diagnostics::field("events", events),
                  diagnostics::field("markers", markers),
                  diagnostics::field("named", boneNames.size()),
                  diagnostics::field("marks", held_.canvasBoxes.size())});
}

} // namespace rawframe::studio
