#include "rawframe/audio/layout.h"

#include "fields.h"
#include "rawframe/document/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <utility>

namespace rawframe::audio {

namespace {

using document::invalid;
using document::Record;
using document::Value;

constexpr std::array<std::string_view, 4> kDocumentFields = {"kind", "formatVersion", "master", "concurrency"};
constexpr std::array<std::string_view, 8> kBusFields = {
    "busId", "name", "role", "volume", "muted", "effects", "sends", "children"};
constexpr std::array<std::string_view, 3> kSendFields = {"target", "level", "position"};
constexpr std::array<std::string_view, 3> kGainFields = {"type", "bypass", "level"};
constexpr std::array<std::string_view, 6> kFilterFields = {"type", "bypass", "shape", "cutoff", "resonance", "slope"};
constexpr std::array<std::string_view, 6> kDelayFields = {"type", "bypass", "time", "feedback", "mix", "offset"};
constexpr std::array<std::string_view, 3> kEqFields = {"type", "bypass", "bands"};
constexpr std::array<std::string_view, 4> kBandFields = {"shape", "frequency", "gain", "q"};
constexpr std::array<std::string_view, 10> kDynamicsFields = {
    "type", "bypass", "processor", "threshold", "ratio", "attack", "release", "makeup", "knee", "key"};
constexpr std::array<std::string_view, 10> kReverbFields = {
    "type", "bypass", "decay", "preDelay", "early", "late", "damping", "density", "diffusion", "mix"};
constexpr std::array<std::string_view, 3> kConcurrencyFields = {"name", "maximumInstances", "resolution"};

/// The longest delay an effect may hold, in seconds.
constexpr double kMaximumDelay = 2.0;

constexpr std::array<std::pair<std::string_view, Role>, 7> kRoles = {{
    {"none", Role::None},
    {"master", Role::Master},
    {"music", Role::Music},
    {"sfx", Role::Sfx},
    {"voice", Role::Voice},
    {"ui", Role::Ui},
    {"ambience", Role::Ambience},
}};

constexpr std::array<std::pair<std::string_view, Resolution>, 5> kResolutions = {{
    {"stop_farthest_then_oldest", Resolution::StopFarthestThenOldest},
    {"prevent_new", Resolution::PreventNew},
    {"stop_oldest", Resolution::StopOldest},
    {"stop_quietest", Resolution::StopQuietest},
    {"stop_lowest_priority_then_oldest", Resolution::StopLowestPriorityThenOldest},
}};

constexpr std::array<std::pair<std::string_view, BandShape>, 4> kBandShapes = {{
    {"low_shelf", BandShape::LowShelf},
    {"high_shelf", BandShape::HighShelf},
    {"peak", BandShape::Peak},
    {"notch", BandShape::Notch},
}};

constexpr std::array<std::pair<std::string_view, Processor>, 5> kProcessors = {{
    {"compressor", Processor::Compressor},
    {"limiter", Processor::Limiter},
    {"expander", Processor::Expander},
    {"gate", Processor::Gate},
    {"upwards_compressor", Processor::UpwardsCompressor},
}};

/// A required word from a closed set, which has no default.
template <typename Enum, std::size_t Count>
result::Result<Enum> requiredWord(const Record& record,
                                  std::string_view field,
                                  const std::array<std::pair<std::string_view, Enum>, Count>& words,
                                  std::string_view why) {
    RAWFRAME_TRY_ASSIGN(const std::string_view kWord, record.text(field));
    for (const auto& [kName, kValue] : words) {
        if (kName == kWord) {
            return kValue;
        }
    }
    return invalid(record.pathOf(field), why);
}

/// A number within bounds; with no fallback, a required one.
result::Result<float> within(const Record& record,
                             std::string_view field,
                             std::optional<double> fallback,
                             double lowest,
                             double highest,
                             std::string_view why) {
    double number = 0;
    if (fallback.has_value()) {
        RAWFRAME_TRY_ASSIGN(number, record.real(field, *fallback));
    } else {
        RAWFRAME_TRY_ASSIGN(const Value* value, record.required(field, Value::Kind::Number));
        number = *value->real();
    }
    if (!(number >= lowest && number <= highest)) {
        return invalid(record.pathOf(field), why);
    }
    return static_cast<float>(number);
}

result::Result<Effect> readEq(const Record& record, const LayoutLimits& limits) {
    Effect effect;
    effect.type = EffectType::ParametricEq;
    RAWFRAME_TRY_ASSIGN(effect.bypass, record.truth("bypass", false));
    RAWFRAME_TRY_ASSIGN(const Value* bands, record.required("bands", Value::Kind::Array));
    if (bands->items().empty() || bands->items().size() > limits.maximumEqBands) {
        return invalid(record.pathOf("bands"), "an equalizer has one band or more, up to the limit");
    }
    for (std::size_t index = 0; index < bands->items().size(); ++index) {
        const std::string kPath = record.pathOf("bands") + "[" + std::to_string(index) + "]";
        RAWFRAME_TRY_ASSIGN(const Record kBand, Record::of(bands->items()[index], kBandFields, kPath));
        EqBand band;
        RAWFRAME_TRY_ASSIGN(
            band.shape, requiredWord(kBand, "shape", kBandShapes, "a band is low_shelf, high_shelf, peak, or notch"));
        RAWFRAME_TRY_ASSIGN(band.frequency,
                            within(kBand, "frequency", std::nullopt, 10, 24000, "a frequency is 10 to 24000 hertz"));
        RAWFRAME_TRY_ASSIGN(band.gain, within(kBand, "gain", 0.0, -24, 24, "a band's gain is -24 to 24 decibels"));
        if (band.shape == BandShape::Notch && band.gain != 0) {
            return invalid(kBand.pathOf("gain"), "a notch has no gain");
        }
        RAWFRAME_TRY_ASSIGN(band.q, within(kBand, "q", 0.7071, 0.1, 20, "a Q is 0.1 to 20"));
        effect.bands.push_back(band);
    }
    return effect;
}

result::Result<Effect> readDynamics(const Record& record, std::optional<std::uint64_t>& key) {
    Effect effect;
    effect.type = EffectType::Dynamics;
    Dynamics& dynamics = effect.dynamics;
    RAWFRAME_TRY_ASSIGN(effect.bypass, record.truth("bypass", false));
    RAWFRAME_TRY_ASSIGN(dynamics.processor,
                        requiredWord(record,
                                     "processor",
                                     kProcessors,
                                     "a processor is compressor, limiter, expander, gate, or upwards_compressor"));
    RAWFRAME_TRY_ASSIGN(dynamics.threshold,
                        within(record, "threshold", std::nullopt, -80, 0, "a threshold is -80 to 0 decibels"));
    const bool kRatioless = dynamics.processor == Processor::Limiter || dynamics.processor == Processor::Gate;
    RAWFRAME_TRY_ASSIGN(const Value* ratio, record.optional("ratio", Value::Kind::Number));
    if (kRatioless && ratio != nullptr) {
        return invalid(record.pathOf("ratio"), "a limiter and a gate have no ratio");
    }
    RAWFRAME_TRY_ASSIGN(dynamics.ratio, within(record, "ratio", 4.0, 1, 50, "a ratio is 1 to 50"));
    RAWFRAME_TRY_ASSIGN(dynamics.attack,
                        within(record, "attack", 0.01, 0.0001, 1, "an attack is a tenth of a millisecond to a second"));
    RAWFRAME_TRY_ASSIGN(dynamics.release,
                        within(record, "release", 0.1, 0.001, 5, "a release is a millisecond to five seconds"));
    RAWFRAME_TRY_ASSIGN(dynamics.makeup, within(record, "makeup", 0.0, -24, 24, "makeup is -24 to 24 decibels"));
    RAWFRAME_TRY_ASSIGN(dynamics.knee, within(record, "knee", 0.0, 0, 24, "a knee is 0 to 24 decibels wide"));
    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kKey, record.optionalText("key"));
    if (kKey == "own_input") {
        return document::notCanonical(record.pathOf("key"), "a field at its default is omitted");
    }
    if (kKey.has_value()) {
        key = parseIdentity(*kKey);
        if (!key.has_value()) {
            return invalid(record.pathOf("key"), "a key is own_input or a bus identity");
        }
    }
    return effect;
}

result::Result<Effect> readReverb(const Record& record) {
    Effect effect;
    effect.type = EffectType::Reverb;
    Reverb& reverb = effect.reverb;
    RAWFRAME_TRY_ASSIGN(effect.bypass, record.truth("bypass", false));
    RAWFRAME_TRY_ASSIGN(reverb.decay, within(record, "decay", std::nullopt, 0.1, 20, "a decay is 0.1 to 20 seconds"));
    RAWFRAME_TRY_ASSIGN(reverb.preDelay,
                        within(record, "preDelay", 0.02, 0, 0.5, "a pre-delay is nought to half a second"));
    RAWFRAME_TRY_ASSIGN(reverb.early, within(record, "early", -6.0, -96, 24, "a level is -96 to 24 decibels"));
    RAWFRAME_TRY_ASSIGN(reverb.late, within(record, "late", 0.0, -96, 24, "a level is -96 to 24 decibels"));
    RAWFRAME_TRY_ASSIGN(reverb.damping, within(record, "damping", 0.5, 0, 1, "damping is nought to one"));
    RAWFRAME_TRY_ASSIGN(reverb.density, within(record, "density", 1.0, 0, 1, "density is nought to one"));
    RAWFRAME_TRY_ASSIGN(reverb.diffusion, within(record, "diffusion", 1.0, 0, 1, "diffusion is nought to one"));
    RAWFRAME_TRY_ASSIGN(reverb.mix, within(record, "mix", 0.3, 0, 1, "a mix is nought to one"));
    return effect;
}

/// Reads one effect; a dynamics effect keyed by another bus leaves that
/// bus's identity in `key`, to resolve once every bus is read.
result::Result<Effect>
readEffect(const Value& value, const std::string& path, const LayoutLimits& limits, std::optional<std::uint64_t>& key) {
    const Value* named = value.find("type");
    if (named == nullptr || named->kind() != Value::Kind::String) {
        return invalid(path + ".type", "an effect names its type");
    }
    const std::string_view kType = *named->text();
    Effect effect;
    if (kType == "rawframe/gain@1") {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kGainFields, path));
        RAWFRAME_TRY_ASSIGN(effect.bypass, kRecord.truth("bypass", false));
        RAWFRAME_TRY_ASSIGN(effect.level, decibels(kRecord, "level"));
        effect.type = EffectType::Gain;
        return effect;
    }
    if (kType == "rawframe/filter@1") {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kFilterFields, path));
        effect.type = EffectType::Filter;
        RAWFRAME_TRY_ASSIGN(effect.bypass, kRecord.truth("bypass", false));
        RAWFRAME_TRY_ASSIGN(const std::string_view kShape, kRecord.text("shape"));
        if (kShape == "low_pass") {
            effect.shape = FilterShape::LowPass;
        } else if (kShape == "high_pass") {
            effect.shape = FilterShape::HighPass;
        } else if (kShape == "band_pass") {
            effect.shape = FilterShape::BandPass;
        } else {
            return invalid(kRecord.pathOf("shape"), "a filter is low_pass, high_pass, or band_pass");
        }
        RAWFRAME_TRY_ASSIGN(const Value* cutoff, kRecord.required("cutoff", Value::Kind::Number));
        const double kCutoff = *cutoff->real();
        if (!(kCutoff >= 10 && kCutoff <= 24000)) {
            return invalid(kRecord.pathOf("cutoff"), "a cutoff is 10 to 24000 hertz");
        }
        effect.cutoff = static_cast<float>(kCutoff);
        RAWFRAME_TRY_ASSIGN(const double kResonance, kRecord.real("resonance", 0.7071));
        if (!(kResonance >= 0.1 && kResonance <= 20)) {
            return invalid(kRecord.pathOf("resonance"), "a resonance is a Q of 0.1 to 20");
        }
        effect.resonance = static_cast<float>(kResonance);
        RAWFRAME_TRY_ASSIGN(const std::int64_t kSlope, kRecord.integer("slope", 12));
        if (kSlope != 12 && kSlope != 24) {
            return invalid(kRecord.pathOf("slope"), "a slope is 12 or 24 decibels an octave");
        }
        effect.slope = static_cast<std::uint8_t>(kSlope);
        return effect;
    }
    if (kType == "rawframe/delay@1") {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kDelayFields, path));
        effect.type = EffectType::Delay;
        RAWFRAME_TRY_ASSIGN(effect.bypass, kRecord.truth("bypass", false));
        RAWFRAME_TRY_ASSIGN(const Value* time, kRecord.required("time", Value::Kind::Number));
        const double kTime = *time->real();
        RAWFRAME_TRY_ASSIGN(const double kFeedback, kRecord.real("feedback", 0.0));
        RAWFRAME_TRY_ASSIGN(const double kMix, kRecord.real("mix", 0.5));
        RAWFRAME_TRY_ASSIGN(const double kOffset, kRecord.real("offset", 0.0));
        if (!(kTime > 0 && kOffset >= 0 && kTime + kOffset <= kMaximumDelay)) {
            return invalid(path, "a delay's time is above nought and, with its offset, at most two seconds");
        }
        if (!(kFeedback >= 0 && kFeedback < 1) || !(kMix >= 0 && kMix <= 1)) {
            return invalid(path, "a delay's feedback is below one and its mix within nought and one");
        }
        effect.time = static_cast<float>(kTime);
        effect.feedback = static_cast<float>(kFeedback);
        effect.mix = static_cast<float>(kMix);
        effect.offset = static_cast<float>(kOffset);
        return effect;
    }
    if (kType == "rawframe/parametric_eq@1") {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kEqFields, path));
        return readEq(kRecord, limits);
    }
    if (kType == "rawframe/dynamics@1") {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kDynamicsFields, path));
        return readDynamics(kRecord, key);
    }
    if (kType == "rawframe/reverb@1") {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kReverbFields, path));
        return readReverb(kRecord);
    }
    return invalid(path + ".type", "no effect of that type");
}

/// A send as read, its target still an identity.
struct PendingSend {
    std::size_t bus = 0;
    std::uint64_t target = 0;
    Send send;
    std::string path;
};

/// A dynamics effect's key as read, still an identity.
struct PendingKey {
    std::size_t bus = 0;
    std::size_t effect = 0;
    std::uint64_t key = 0;
    std::string path;
};

class Reader {
public:
    explicit Reader(const LayoutLimits& limits) noexcept : limits_(limits) {
    }

    result::Status bus(const Value& value, const std::string& path, std::size_t parent, std::size_t depth) {
        if (depth >= limits_.maximumDepth) {
            return invalid(path, "the bus tree is deeper than allowed");
        }
        if (layout.buses.size() >= limits_.maximumBuses) {
            return invalid(path, "more buses than allowed");
        }
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kBusFields, path));
        Bus made;
        RAWFRAME_TRY_ASSIGN(const std::string_view kId, kRecord.text("busId"));
        const auto kParsed = parseIdentity(kId);
        if (!kParsed) {
            return invalid(kRecord.pathOf("busId"), "an identity is 16 lowercase hexadecimal digits");
        }
        made.id = *kParsed;
        if (layout.busWithId(made.id)) {
            return invalid(kRecord.pathOf("busId"), "a bus identity is used once");
        }
        RAWFRAME_TRY_ASSIGN(const std::string_view kName, kRecord.text("name"));
        if (!machineName(kName, limits_.maximumNameLength)) {
            return invalid(kRecord.pathOf("name"),
                           "a name is a lowercase letter, then letters, digits, or underscores");
        }
        if (std::ranges::contains(layout.buses, kName, &Bus::name)) {
            return invalid(kRecord.pathOf("name"), "a bus name is used once");
        }
        made.name = std::string{kName};
        RAWFRAME_TRY_ASSIGN(made.role, closedWord(kRecord, "role", kRoles));
        const bool kRoot = layout.buses.empty();
        if ((made.role == Role::Master) != kRoot) {
            return invalid(kRecord.pathOf("role"), "the root bus, and only it, is the master");
        }
        if (made.role != Role::None && layout.busWithRole(made.role)) {
            return invalid(kRecord.pathOf("role"), "a role is one bus's");
        }
        RAWFRAME_TRY_ASSIGN(made.volume, decibels(kRecord, "volume"));
        RAWFRAME_TRY_ASSIGN(made.muted, kRecord.truth("muted", false));
        RAWFRAME_TRY_ASSIGN(const Value* effects, kRecord.optional("effects", Value::Kind::Array));
        if (effects != nullptr) {
            if (effects->items().empty()) {
                return document::notCanonical(kRecord.pathOf("effects"), "a field at its default is omitted");
            }
            if (effects->items().size() > limits_.maximumEffectsPerBus) {
                return invalid(kRecord.pathOf("effects"), "more effects than allowed");
            }
            for (std::size_t index = 0; index < effects->items().size(); ++index) {
                const std::string kPath = kRecord.pathOf("effects") + "[" + std::to_string(index) + "]";
                std::optional<std::uint64_t> key;
                RAWFRAME_TRY_ASSIGN(Effect effect, readEffect(effects->items()[index], kPath, limits_, key));
                if (key.has_value()) {
                    keys_.push_back(
                        PendingKey{.bus = layout.buses.size(), .effect = index, .key = *key, .path = kPath + ".key"});
                }
                made.effects.push_back(std::move(effect));
            }
        }
        const std::size_t kIndex = layout.buses.size();
        made.parent = kRoot ? kIndex : parent;
        RAWFRAME_TRY_ASSIGN(const Value* sends, kRecord.optional("sends", Value::Kind::Array));
        if (sends != nullptr) {
            if (sends->items().empty()) {
                return document::notCanonical(kRecord.pathOf("sends"), "a field at its default is omitted");
            }
            if (sends->items().size() > limits_.maximumSendsPerBus) {
                return invalid(kRecord.pathOf("sends"), "more sends than allowed");
            }
            for (std::size_t index = 0; index < sends->items().size(); ++index) {
                const std::string kPath = kRecord.pathOf("sends") + "[" + std::to_string(index) + "]";
                RAWFRAME_TRY_ASSIGN(const Record kSend, Record::of(sends->items()[index], kSendFields, kPath));
                PendingSend pending{.bus = kIndex, .target = 0, .send = {}, .path = kPath};
                RAWFRAME_TRY_ASSIGN(const std::string_view kTarget, kSend.text("target"));
                const auto kTargetId = parseIdentity(kTarget);
                if (!kTargetId) {
                    return invalid(kSend.pathOf("target"), "an identity is 16 lowercase hexadecimal digits");
                }
                pending.target = *kTargetId;
                RAWFRAME_TRY_ASSIGN(pending.send.level, decibels(kSend, "level"));
                RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kPosition, kSend.optionalText("position"));
                if (kPosition == "post_fader") {
                    return document::notCanonical(kSend.pathOf("position"), "a field at its default is omitted");
                }
                if (kPosition && *kPosition != "pre_fader") {
                    return invalid(kSend.pathOf("position"), "a send is pre_fader or post_fader");
                }
                pending.send.position = kPosition ? SendPosition::PreFader : SendPosition::PostFader;
                pending_.push_back(std::move(pending));
            }
        }
        layout.buses.push_back(std::move(made));
        RAWFRAME_TRY_ASSIGN(const Value* children, kRecord.optional("children", Value::Kind::Array));
        if (children != nullptr) {
            if (children->items().empty()) {
                return document::notCanonical(kRecord.pathOf("children"), "a field at its default is omitted");
            }
            for (std::size_t index = 0; index < children->items().size(); ++index) {
                RAWFRAME_TRY(bus(children->items()[index],
                                 kRecord.pathOf("children") + "[" + std::to_string(index) + "]",
                                 kIndex,
                                 depth + 1));
            }
        }
        return {};
    }

    result::Status sends() {
        for (PendingSend& pending : pending_) {
            const auto kTarget = layout.busWithId(pending.target);
            if (!kTarget) {
                return invalid(pending.path + ".target", "no bus has that identity");
            }
            if (*kTarget == pending.bus) {
                return invalid(pending.path + ".target", "a bus does not send to itself");
            }
            pending.send.target = *kTarget;
            layout.buses[pending.bus].sends.push_back(pending.send);
        }
        for (const PendingKey& pending : keys_) {
            const auto kKey = layout.busWithId(pending.key);
            if (!kKey) {
                return invalid(pending.path, "no bus has that identity");
            }
            if (*kKey == pending.bus) {
                return invalid(pending.path, "a bus's own signal is own_input");
            }
            layout.buses[pending.bus].effects[pending.effect].dynamics.key = *kKey;
        }
        if (layout.mixOrder().size() != layout.buses.size()) {
            return invalid("$.master", "sends or keys make a loop: a bus would wait on itself");
        }
        return {};
    }

    Layout layout;

private:
    LayoutLimits limits_;
    std::vector<PendingSend> pending_;
    std::vector<PendingKey> keys_;
};

} // namespace

std::optional<std::size_t> Layout::busWithId(std::uint64_t id) const noexcept {
    const auto kFound = std::ranges::find(buses, id, &Bus::id);
    return kFound == buses.end() ? std::nullopt : std::optional{static_cast<std::size_t>(kFound - buses.begin())};
}

std::optional<std::size_t> Layout::busWithRole(Role role) const noexcept {
    const auto kFound = std::ranges::find(buses, role, &Bus::role);
    return kFound == buses.end() ? std::nullopt : std::optional{static_cast<std::size_t>(kFound - buses.begin())};
}

std::vector<std::size_t> Layout::mixOrder() const {
    // Feeds: a child feeds its parent, a sender its target, a key the bus
    // it keys. Kahn's order, lowest index first among the ready, so the
    // order is always the same.
    std::vector<std::size_t> waiting(buses.size(), 0);
    std::vector<std::vector<std::size_t>> keyed(buses.size());
    for (std::size_t index = 0; index < buses.size(); ++index) {
        if (buses[index].parent != index) {
            ++waiting[buses[index].parent];
        }
        for (const Send& send : buses[index].sends) {
            ++waiting[send.target];
        }
        for (const Effect& effect : buses[index].effects) {
            if (effect.type == EffectType::Dynamics && effect.dynamics.key.has_value()) {
                ++waiting[index];
                keyed[*effect.dynamics.key].push_back(index);
            }
        }
    }
    std::set<std::size_t> ready;
    for (std::size_t index = 0; index < buses.size(); ++index) {
        if (waiting[index] == 0) {
            ready.insert(index);
        }
    }
    std::vector<std::size_t> order;
    while (!ready.empty()) {
        const std::size_t kBus = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(kBus);
        const auto kFed = [&](std::size_t fed) {
            if (--waiting[fed] == 0) {
                ready.insert(fed);
            }
        };
        if (buses[kBus].parent != kBus) {
            kFed(buses[kBus].parent);
        }
        for (const Send& send : buses[kBus].sends) {
            kFed(send.target);
        }
        for (const std::size_t kKeyed : keyed[kBus]) {
            kFed(kKeyed);
        }
    }
    return order;
}

result::Result<Layout> readLayout(std::string_view text, const LayoutLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Value kRoot, document::parseCanonical(text));
    const Value* version = kRoot.find("formatVersion");
    if (version == nullptr || version->integer() != 1) {
        return invalid("formatVersion", "the format version is 1");
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(kRoot, kDocumentFields, "$"));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    if (kKind != "audio.mixer") {
        return invalid(kRecord.pathOf("kind"), "the kind is audio.mixer");
    }
    Reader reader{limits};
    RAWFRAME_TRY_ASSIGN(const Value* master, kRecord.required("master", Value::Kind::Object));
    RAWFRAME_TRY(reader.bus(*master, kRecord.pathOf("master"), 0, 0));
    RAWFRAME_TRY(reader.sends());

    RAWFRAME_TRY_ASSIGN(const Value* sets, kRecord.optional("concurrency", Value::Kind::Array));
    if (sets != nullptr) {
        if (sets->items().empty()) {
            return document::notCanonical(kRecord.pathOf("concurrency"), "a field at its default is omitted");
        }
        if (sets->items().size() > limits.maximumConcurrencySets) {
            return invalid(kRecord.pathOf("concurrency"), "more concurrency sets than allowed");
        }
        for (std::size_t index = 0; index < sets->items().size(); ++index) {
            const std::string kPath = kRecord.pathOf("concurrency") + "[" + std::to_string(index) + "]";
            RAWFRAME_TRY_ASSIGN(const Record kSet, Record::of(sets->items()[index], kConcurrencyFields, kPath));
            ConcurrencySet set;
            RAWFRAME_TRY_ASSIGN(const std::string_view kName, kSet.text("name"));
            if (!machineName(kName, limits.maximumNameLength)) {
                return invalid(kSet.pathOf("name"),
                               "a name is a lowercase letter, then letters, digits, or underscores");
            }
            if (std::ranges::contains(reader.layout.concurrency, kName, &ConcurrencySet::name)) {
                return invalid(kSet.pathOf("name"), "a concurrency set's name is used once");
            }
            set.name = std::string{kName};
            RAWFRAME_TRY_ASSIGN(const std::int64_t kMaximum, kSet.integer("maximumInstances"));
            if (kMaximum < 1 || kMaximum > 1024) {
                return invalid(kSet.pathOf("maximumInstances"), "a set holds 1 to 1024 instances");
            }
            set.maximumInstances = static_cast<std::size_t>(kMaximum);
            RAWFRAME_TRY_ASSIGN(set.resolution, closedWord(kSet, "resolution", kResolutions));
            reader.layout.concurrency.push_back(std::move(set));
        }
    }
    return std::move(reader.layout);
}

float gainOf(float decibels) noexcept {
    return std::pow(10.0F, decibels / 20.0F);
}

} // namespace rawframe::audio
