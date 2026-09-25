#include "rawframe/animation/clip.h"

#include "rawframe/animation/errors.h"
#include "rawframe/document/json.h"
#include "text.h"

#include <algorithm>
#include <array>
#include <map>
#include <span>
#include <tuple>

namespace rawframe::animation {

namespace {

using document::Value;

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::ClipInvalid), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::OverLimit), why);
}

constexpr std::array<std::string_view, 3> kChannels{"translation", "rotation", "scale"};
constexpr std::array<std::string_view, 3> kInterpolations{"linear", "step", "cubic"};
constexpr std::array<std::string_view, 2> kLoops{"clamp", "loop"};
constexpr std::array<std::string_view, 2> kRelevances{"presentation", "simulation"};

/// The place of `text` among `names`, if it is one.
template <std::size_t Count>
std::optional<std::size_t> placeOf(const std::array<std::string_view, Count>& names, const Value* value) {
    if (value == nullptr || value->kind() != Value::Kind::String) {
        return std::nullopt;
    }
    const auto kFound = std::ranges::find(names, *value->text());
    if (kFound == names.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(kFound - names.begin());
}

/// Within the clip: up to its duration, short of it when it loops.
bool within(const Clip& clip, double time) {
    return time >= 0.0 && (clip.loop == Loop::Loop ? time < clip.duration : time <= clip.duration);
}

result::Status keysInForm(const Clip& clip, const Track& track, const ClipLimits& limits) {
    if (track.keys.size() > limits.maximumKeys) {
        return overLimit("a track has more keys than its limit");
    }
    if (track.keys.empty()) {
        return invalid("a track has a key");
    }
    const std::size_t kWidth = widthOf(track.channel);
    for (std::size_t at = 0; at < track.keys.size(); ++at) {
        const Key& key = track.keys[at];
        if (!within(clip, key.time) || (at > 0 && !(track.keys[at - 1].time < key.time))) {
            return invalid("a track's keys are in strictly increasing time within the clip, short of its duration "
                           "when it loops");
        }
        // What a key does not use is nought, so one key has one form.
        const std::span<const double> kValue{key.value};
        const bool kCubic = key.interpolation == Interpolation::Cubic;
        const bool kUnused = std::ranges::all_of(kValue.subspan(kWidth),
                                                 [](double each) {
                                                     return each == 0.0;
                                                 }) &&
                             (kCubic ? std::ranges::all_of(std::span{key.in}.subspan(kWidth),
                                                           [](double each) {
                                                               return each == 0.0;
                                                           }) &&
                                           std::ranges::all_of(std::span{key.out}.subspan(kWidth),
                                                               [](double each) {
                                                                   return each == 0.0;
                                                               })
                                     : key.in == std::array<double, 4>{} && key.out == std::array<double, 4>{});
        if (!finite(key.value) || !finite(key.in) || !finite(key.out) || !kUnused) {
            return invalid("a key's value and tangents are finite numbers, as many as its channel has");
        }
        if (track.channel == Channel::Rotation && (kCubic || !unit(key.value))) {
            return invalid("a rotation's keys are unit quaternions, stepped or turned by slerp");
        }
    }
    return {};
}

result::Status eventsInForm(const Clip& clip) {
    std::map<std::uint64_t, std::string_view> names;
    std::map<std::string_view, std::uint64_t> events;
    for (std::size_t at = 0; at < clip.events.size(); ++at) {
        const ClipEvent& event = clip.events[at];
        if (!within(clip, event.time) || (at > 0 && clip.events[at - 1].time > event.time)) {
            return invalid("a clip's events are in time order within it");
        }
        if (event.event == 0 || !machineName(event.name)) {
            return invalid("an event is its identity and a machine name");
        }
        const auto [kName, kNewName] = names.emplace(event.event, event.name);
        const auto [kEvent, kNewEvent] = events.emplace(event.name, event.event);
        if ((!kNewName && kName->second != event.name) || (!kNewEvent && kEvent->second != event.event)) {
            return invalid("an event's identity and name are one to each other within a clip");
        }
    }
    for (std::size_t at = 0; at < clip.syncMarkers.size(); ++at) {
        const SyncMarker& marker = clip.syncMarkers[at];
        if (!within(clip, marker.time) || !machineName(marker.name) ||
            (at > 0 && clip.syncMarkers[at - 1].time > marker.time)) {
            return invalid("a clip's sync markers are machine names in time order within it");
        }
    }
    return {};
}

Value keyOf(const Key& key, std::size_t width) {
    Value made = Value::object();
    made.add("time", Value::real(key.time));
    made.add("value", arrayOf(key.value, width));
    if (key.interpolation != Interpolation::Linear) {
        made.add("interpolation",
                 Value::string(std::string{kInterpolations[static_cast<std::size_t>(key.interpolation)]}));
    }
    if (key.interpolation == Interpolation::Cubic) {
        made.add("in", arrayOf(key.in, width));
        made.add("out", arrayOf(key.out, width));
    }
    return made;
}

result::Result<Key> readKey(const Value& each, std::size_t width) {
    const Value* interpolation = each.find("interpolation");
    const std::optional<std::size_t> kInterpolation =
        interpolation != nullptr ? placeOf(kInterpolations, interpolation) : std::optional<std::size_t>{0};
    const bool kCubic = kInterpolation == static_cast<std::size_t>(Interpolation::Cubic);
    const bool kShape = kCubic                     ? hasMembers(each, {"time", "value", "interpolation", "in", "out"})
                        : interpolation != nullptr ? hasMembers(each, {"time", "value", "interpolation"})
                                                   : hasMembers(each, {"time", "value"});
    const std::optional<double> kTime = kShape ? numberOf(each.find("time")) : std::nullopt;
    const auto kValue = kShape ? numbersOf(each.find("value"), width) : std::nullopt;
    const auto kIn = kShape && kCubic ? numbersOf(each.find("in"), width) : std::optional<std::array<double, 4>>{};
    const auto kOut = kShape && kCubic ? numbersOf(each.find("out"), width) : std::optional<std::array<double, 4>>{};
    if (!kInterpolation.has_value() || !kTime.has_value() || !kValue.has_value() ||
        (kCubic && (!kIn.has_value() || !kOut.has_value()))) {
        return invalid("a key is a time, a value, and optionally an interpolation, with tangents when cubic");
    }
    return Key{.time = *kTime,
               .value = *kValue,
               .interpolation = static_cast<Interpolation>(*kInterpolation),
               .in = kIn.value_or(std::array<double, 4>{}),
               .out = kOut.value_or(std::array<double, 4>{})};
}

result::Result<Track> readTrack(const Value& each, const ClipLimits& limits) {
    const std::optional<base::Bits128> kBone =
        hasMembers(each, {"bone", "channel", "keys"}) ? bits128Of(each.find("bone")) : std::nullopt;
    const std::optional<std::size_t> kChannel =
        kBone.has_value() ? placeOf(kChannels, each.find("channel")) : std::nullopt;
    if (!kChannel.has_value() || each.find("keys")->kind() != Value::Kind::Array) {
        return invalid("a track is a bone, a channel, and keys");
    }
    const Value& keys = *each.find("keys");
    if (keys.items().size() > limits.maximumKeys) {
        return overLimit("a track has more keys than its limit");
    }
    Track track{.bone = *kBone, .channel = static_cast<Channel>(*kChannel), .keys = {}};
    for (const Value& key : keys.items()) {
        RAWFRAME_TRY_ASSIGN(Key made, readKey(key, widthOf(track.channel)));
        track.keys.push_back(made);
    }
    return track;
}

} // namespace

result::Status validate(const Clip& clip, const ClipLimits& limits) {
    if (clip.tracks.size() > limits.maximumTracks) {
        return overLimit("a clip has more tracks than its limit");
    }
    if (clip.events.size() + clip.syncMarkers.size() > limits.maximumEvents) {
        return overLimit("a clip has more events and sync markers than its limit");
    }
    if (!(clip.duration > 0.0) || !finite(std::span{&clip.duration, 1})) {
        return invalid("a clip's duration is a finite number of seconds past nought");
    }
    if (clip.skeleton == base::Bits128{} || (!clip.tracks.empty() && !clip.skeleton.has_value())) {
        return invalid("a clip with a bone's track names its skeleton");
    }
    std::vector<std::tuple<base::Bits128, Channel>> bindings;
    for (const Track& track : clip.tracks) {
        if (track.bone == base::Bits128{}) {
            return invalid("a track names its bone");
        }
        RAWFRAME_TRY(keysInForm(clip, track, limits));
        bindings.emplace_back(track.bone, track.channel);
    }
    std::ranges::sort(bindings);
    if (std::ranges::adjacent_find(bindings) != bindings.end()) {
        return invalid("a clip has one track for a bone's channel");
    }
    return eventsInForm(clip);
}

result::Result<std::string> writeClip(const Clip& clip, const ClipLimits& limits) {
    RAWFRAME_TRY(validate(clip, limits));
    Value tracks = Value::array();
    for (const Track& track : clip.tracks) {
        Value keys = Value::array();
        for (const Key& key : track.keys) {
            keys.push(keyOf(key, widthOf(track.channel)));
        }
        Value made = Value::object();
        made.add("bone", Value::string(hexOf(track.bone)));
        made.add("channel", Value::string(std::string{kChannels[static_cast<std::size_t>(track.channel)]}));
        made.add("keys", std::move(keys));
        tracks.push(std::move(made));
    }
    Value events = Value::array();
    for (const ClipEvent& event : clip.events) {
        Value made = Value::object();
        made.add("event", Value::string(hexOf(event.event)));
        made.add("name", Value::string(event.name));
        made.add("time", Value::real(event.time));
        made.add("relevance", Value::string(std::string{kRelevances[static_cast<std::size_t>(event.relevance)]}));
        events.push(std::move(made));
    }
    Value markers = Value::array();
    for (const SyncMarker& marker : clip.syncMarkers) {
        Value made = Value::object();
        made.add("name", Value::string(marker.name));
        made.add("time", Value::real(marker.time));
        markers.push(std::move(made));
    }
    Value made = Value::object();
    made.add("formatVersion", Value::integer(1));
    made.add("kind", Value::string("animation.clip"));
    if (clip.skeleton.has_value()) {
        made.add("skeleton", Value::string(hexOf(*clip.skeleton)));
    }
    made.add("duration", Value::real(clip.duration));
    made.add("loop", Value::string(std::string{kLoops[static_cast<std::size_t>(clip.loop)]}));
    made.add("tracks", std::move(tracks));
    made.add("events", std::move(events));
    made.add("syncMarkers", std::move(markers));
    return document::write(made);
}

result::Result<Clip> readClip(std::string_view text, const ClipLimits& limits) {
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error()};
    }
    const Value* kind = parsed->find("kind");
    const bool kBound = parsed->find("skeleton") != nullptr;
    const bool kShape =
        kBound
            ? hasMembers(*parsed,
                         {"formatVersion", "kind", "skeleton", "duration", "loop", "tracks", "events", "syncMarkers"})
            : hasMembers(*parsed, {"formatVersion", "kind", "duration", "loop", "tracks", "events", "syncMarkers"});
    if (!kShape || kind->text() == nullptr || *kind->text() != "animation.clip" ||
        parsed->find("formatVersion")->integer() != 1) {
        return invalid("a clip is format version 1, kind animation.clip, an optional skeleton, a duration, a loop, "
                       "tracks, events, and sync markers");
    }
    const std::optional<base::Bits128> kSkeleton = kBound ? bits128Of(parsed->find("skeleton")) : std::nullopt;
    const std::optional<double> kDuration = numberOf(parsed->find("duration"));
    const std::optional<std::size_t> kLoop = placeOf(kLoops, parsed->find("loop"));
    const Value& tracks = *parsed->find("tracks");
    const Value& events = *parsed->find("events");
    const Value& markers = *parsed->find("syncMarkers");
    if ((kBound && !kSkeleton.has_value()) || !kDuration.has_value() || !kLoop.has_value() ||
        tracks.kind() != Value::Kind::Array || events.kind() != Value::Kind::Array ||
        markers.kind() != Value::Kind::Array) {
        return invalid("a clip's skeleton is 32 hex digits, its duration a number, its loop clamp or loop, and its "
                       "tracks, events, and sync markers arrays");
    }
    if (tracks.items().size() > limits.maximumTracks ||
        events.items().size() + markers.items().size() > limits.maximumEvents) {
        return overLimit("a clip has more tracks, or events and sync markers, than its limits");
    }
    Clip clip{.skeleton = kSkeleton, .duration = *kDuration, .loop = static_cast<Loop>(*kLoop)};
    for (const Value& each : tracks.items()) {
        RAWFRAME_TRY_ASSIGN(Track track, readTrack(each, limits));
        clip.tracks.push_back(std::move(track));
    }
    for (const Value& each : events.items()) {
        const std::optional<std::uint64_t> kEvent =
            hasMembers(each, {"event", "name", "time", "relevance"}) ? bits64Of(each.find("event")) : std::nullopt;
        const std::optional<double> kTime = kEvent.has_value() ? numberOf(each.find("time")) : std::nullopt;
        const std::optional<std::size_t> kRelevance =
            kEvent.has_value() ? placeOf(kRelevances, each.find("relevance")) : std::nullopt;
        if (!kTime.has_value() || !kRelevance.has_value() || each.find("name")->kind() != Value::Kind::String) {
            return invalid("an event is its identity, a name, a time, and a relevance");
        }
        clip.events.push_back(ClipEvent{.event = *kEvent,
                                        .name = *each.find("name")->text(),
                                        .time = *kTime,
                                        .relevance = static_cast<Relevance>(*kRelevance)});
    }
    for (const Value& each : markers.items()) {
        const std::optional<double> kTime =
            hasMembers(each, {"name", "time"}) ? numberOf(each.find("time")) : std::nullopt;
        if (!kTime.has_value() || each.find("name")->kind() != Value::Kind::String) {
            return invalid("a sync marker is a name and a time");
        }
        clip.syncMarkers.push_back(SyncMarker{.name = *each.find("name")->text(), .time = *kTime});
    }
    // What the writer makes of it is the text, byte for byte, or the text
    // was not in the one form.
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeClip(clip, limits));
    if (kWritten != text) {
        return invalid("a clip is not in its canonical form");
    }
    return clip;
}

} // namespace rawframe::animation
