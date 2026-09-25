#include "rawframe/audio/sound.h"

#include "fields.h"
#include "rawframe/document/json.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rawframe::audio {

namespace {

using document::invalid;
using document::Record;
using document::Value;

constexpr std::array<std::string_view, 14> kDocumentFields = {"kind",
                                                              "formatVersion",
                                                              "variants",
                                                              "selection",
                                                              "volume",
                                                              "pitch",
                                                              "loop",
                                                              "bus",
                                                              "concurrency",
                                                              "priority",
                                                              "loading",
                                                              "attenuation",
                                                              "virtualization",
                                                              "despawn"};
constexpr std::array<std::string_view, 2> kVariantFields = {"clip", "weight"};
constexpr std::array<std::string_view, 2> kRangeFields = {"minimum", "maximum"};
constexpr std::array<std::string_view, 2> kLoopFields = {"start", "end"};
constexpr std::array<std::string_view, 4> kAttenuationFields = {
    "minimumDistance", "maximumDistance", "falloff", "noListener"};

constexpr std::array<std::pair<std::string_view, Selection>, 3> kSelections = {{
    {"sequential", Selection::Sequential},
    {"random", Selection::Random},
    {"random_no_immediate_repeat", Selection::RandomNoImmediateRepeat},
}};
constexpr std::array<std::pair<std::string_view, Falloff>, 4> kFalloffs = {{
    {"inverse", Falloff::Inverse},
    {"inverse_square", Falloff::InverseSquare},
    {"linear", Falloff::Linear},
    {"logarithmic", Falloff::Logarithmic},
}};
constexpr std::array<std::pair<std::string_view, NoListener>, 2> kNoListeners = {{
    {"silent", NoListener::Silent},
    {"flat_fallback", NoListener::FlatFallback},
}};
constexpr std::array<std::pair<std::string_view, Virtualization>, 3> kVirtualizations = {{
    {"disabled", Virtualization::Disabled},
    {"track_position", Virtualization::TrackPosition},
    {"restart", Virtualization::Restart},
}};
constexpr std::array<std::pair<std::string_view, Despawn>, 3> kDespawns = {{
    {"stop", Despawn::Stop},
    {"fade_out", Despawn::FadeOut},
    {"detach_to_completion", Despawn::DetachToCompletion},
}};

/// The highest pitch ratio a play may draw: three octaves up.
constexpr double kMaximumPitch = 8.0;

/// A `{minimum, maximum}` range; absent, both are `fallback`, and present at
/// that it is not canonical. `check` holds for each end.
template <typename Check>
result::Result<std::pair<float, float>>
range(const Record& record, std::string_view field, double fallback, Check check) {
    RAWFRAME_TRY_ASSIGN(const Value* value, record.optional(field, Value::Kind::Object));
    if (value == nullptr) {
        return std::pair{static_cast<float>(fallback), static_cast<float>(fallback)};
    }
    RAWFRAME_TRY_ASSIGN(const Record kRange, Record::of(*value, kRangeFields, record.pathOf(field)));
    RAWFRAME_TRY_ASSIGN(const Value* minimum, kRange.required("minimum", Value::Kind::Number));
    RAWFRAME_TRY_ASSIGN(const Value* maximum, kRange.required("maximum", Value::Kind::Number));
    const double kMinimum = *minimum->real();
    const double kMaximum = *maximum->real();
    if (kMinimum == fallback && kMaximum == fallback) {
        return document::notCanonical(record.pathOf(field), "a field at its default is omitted");
    }
    if (!check(kMinimum) || !check(kMaximum) || kMinimum > kMaximum) {
        return invalid(record.pathOf(field), "a range's ends are in bounds and its minimum at most its maximum");
    }
    return std::pair{static_cast<float>(kMinimum), static_cast<float>(kMaximum)};
}

} // namespace

result::Result<SoundDeclaration> readSound(std::string_view text, const Layout& layout, const SoundLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Value kRoot, document::parseCanonical(text));
    const Value* version = kRoot.find("formatVersion");
    if (version == nullptr || version->integer() != 1) {
        return invalid("formatVersion", "the format version is 1");
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(kRoot, kDocumentFields, "$"));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    if (kKind != "audio.sound") {
        return invalid(kRecord.pathOf("kind"), "the kind is audio.sound");
    }
    SoundDeclaration sound;

    RAWFRAME_TRY_ASSIGN(const Value* variants, kRecord.required("variants", Value::Kind::Array));
    if (variants->items().empty() || variants->items().size() > limits.maximumVariants) {
        return invalid(kRecord.pathOf("variants"), "a sound has one variant or more, within the limit");
    }
    for (std::size_t index = 0; index < variants->items().size(); ++index) {
        const std::string kPath = kRecord.pathOf("variants") + "[" + std::to_string(index) + "]";
        RAWFRAME_TRY_ASSIGN(const Record kVariant, Record::of(variants->items()[index], kVariantFields, kPath));
        Variant variant;
        RAWFRAME_TRY_ASSIGN(const std::string_view kClip, kVariant.text("clip"));
        // A path beside the declaration, going nowhere above it.
        if (kClip.empty() || kClip.front() == '/' || kClip.find("..") != std::string_view::npos ||
            kClip.find('\\') != std::string_view::npos) {
            return invalid(kVariant.pathOf("clip"), "a clip is a relative path beside the declaration");
        }
        variant.clip = std::string{kClip};
        RAWFRAME_TRY_ASSIGN(const std::int64_t kWeight, kVariant.integer("weight", 1));
        if (kWeight < 1 || kWeight > 1'000'000) {
            return invalid(kVariant.pathOf("weight"), "a weight is 1 to 1000000");
        }
        variant.weight = static_cast<std::uint32_t>(kWeight);
        sound.variants.push_back(std::move(variant));
    }
    RAWFRAME_TRY_ASSIGN(sound.selection, closedWord(kRecord, "selection", kSelections));

    RAWFRAME_TRY_ASSIGN(const auto kVolume, range(kRecord, "volume", 0.0, [](double level) {
                            return std::isfinite(level) && level <= kMaximumDecibels;
                        }));
    sound.volumeMinimum = kVolume.first;
    sound.volumeMaximum = kVolume.second;
    RAWFRAME_TRY_ASSIGN(const auto kPitch, range(kRecord, "pitch", 1.0, [](double pitch) {
                            return pitch > 0 && pitch <= kMaximumPitch;
                        }));
    sound.pitchMinimum = kPitch.first;
    sound.pitchMaximum = kPitch.second;

    RAWFRAME_TRY_ASSIGN(const Value* loop, kRecord.optional("loop", Value::Kind::Object));
    if (loop != nullptr) {
        sound.loop = true;
        RAWFRAME_TRY_ASSIGN(const Record kLoop, Record::of(*loop, kLoopFields, kRecord.pathOf("loop")));
        RAWFRAME_TRY_ASSIGN(const Value* start, kLoop.optional("start", Value::Kind::Number));
        RAWFRAME_TRY_ASSIGN(const Value* end, kLoop.optional("end", Value::Kind::Number));
        if ((start == nullptr) != (end == nullptr)) {
            return invalid(kRecord.pathOf("loop"), "loop points come together");
        }
        if (start != nullptr) {
            const double kStart = *start->real();
            const double kEnd = *end->real();
            if (!(kStart >= 0 && kStart < kEnd && std::isfinite(kEnd))) {
                return invalid(kRecord.pathOf("loop"), "a loop starts at nought or later and ends after it starts");
            }
            sound.loopStart = static_cast<float>(kStart);
            sound.loopEnd = static_cast<float>(kEnd);
        }
    }

    RAWFRAME_TRY_ASSIGN(const std::string_view kBus, kRecord.text("bus"));
    const auto kBusId = parseIdentity(kBus);
    const auto kBusIndex = kBusId ? layout.busWithId(*kBusId) : std::nullopt;
    if (!kBusIndex) {
        return invalid(kRecord.pathOf("bus"), "the layout has no bus of that identity");
    }
    sound.bus = *kBusIndex;
    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kSet, kRecord.optionalText("concurrency"));
    if (kSet) {
        const auto kFound = std::ranges::find(layout.concurrency, *kSet, &ConcurrencySet::name);
        if (kFound == layout.concurrency.end()) {
            return invalid(kRecord.pathOf("concurrency"), "the layout has no concurrency set of that name");
        }
        sound.concurrency = static_cast<std::size_t>(kFound - layout.concurrency.begin());
    }
    RAWFRAME_TRY_ASSIGN(const std::int64_t kPriority, kRecord.integer("priority", 0));
    if (kPriority < -limits.maximumPriority || kPriority > limits.maximumPriority) {
        return invalid(kRecord.pathOf("priority"), "a priority is within the limit either way");
    }
    sound.priority = static_cast<std::int32_t>(kPriority);
    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kLoading, kRecord.optionalText("loading"));
    if (kLoading == "preload") {
        return document::notCanonical(kRecord.pathOf("loading"), "a field at its default is omitted");
    }
    if (kLoading == "stream" || kLoading == "on_demand") {
        return invalid(kRecord.pathOf("loading"), "only preloading is done yet");
    }
    if (kLoading) {
        return invalid(kRecord.pathOf("loading"), "loading is preload, stream, or on_demand");
    }

    RAWFRAME_TRY_ASSIGN(const Value* attenuation, kRecord.optional("attenuation", Value::Kind::Object));
    if (attenuation != nullptr) {
        RAWFRAME_TRY_ASSIGN(const Record kBlock,
                            Record::of(*attenuation, kAttenuationFields, kRecord.pathOf("attenuation")));
        Attenuation made;
        RAWFRAME_TRY_ASSIGN(const Value* minimum, kBlock.required("minimumDistance", Value::Kind::Number));
        RAWFRAME_TRY_ASSIGN(const Value* maximum, kBlock.required("maximumDistance", Value::Kind::Number));
        const double kMinimum = *minimum->real();
        const double kMaximum = *maximum->real();
        if (!(kMinimum > 0 && kMinimum < kMaximum && std::isfinite(kMaximum))) {
            return invalid(kRecord.pathOf("attenuation"), "distances are above nought, the minimum below the maximum");
        }
        made.minimumDistance = static_cast<float>(kMinimum);
        made.maximumDistance = static_cast<float>(kMaximum);
        RAWFRAME_TRY_ASSIGN(made.falloff, closedWord(kBlock, "falloff", kFalloffs));
        RAWFRAME_TRY_ASSIGN(made.noListener, closedWord(kBlock, "noListener", kNoListeners));
        sound.attenuation = made;
    }
    RAWFRAME_TRY_ASSIGN(sound.virtualization, closedWord(kRecord, "virtualization", kVirtualizations));
    RAWFRAME_TRY_ASSIGN(sound.despawn, closedWord(kRecord, "despawn", kDespawns));
    return sound;
}

float attenuate(const Attenuation& attenuation, float distance) noexcept {
    const float kMinimum = attenuation.minimumDistance;
    const float kMaximum = attenuation.maximumDistance;
    // A distance that is not a number is nowhere to be heard.
    if (std::isnan(distance)) {
        return 0;
    }
    if (distance <= kMinimum) {
        return 1;
    }
    if (distance >= kMaximum) {
        return 0;
    }
    switch (attenuation.falloff) {
    case Falloff::Inverse:
        return kMinimum / distance;
    case Falloff::InverseSquare:
        return (kMinimum * kMinimum) / (distance * distance);
    case Falloff::Linear:
        return 1.0F - ((distance - kMinimum) / (kMaximum - kMinimum));
    case Falloff::Logarithmic:
        return 1.0F - (std::log(distance / kMinimum) / std::log(kMaximum / kMinimum));
    }
    return 0;
}

} // namespace rawframe::audio
