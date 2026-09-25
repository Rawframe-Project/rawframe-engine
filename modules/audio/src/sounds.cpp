#include "rawframe/audio/sounds.h"

#include "rawframe/audio/errors.h"
#include "rawframe/world/random.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rawframe::audio {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AudioError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kAudioDomain, code(error), why).error()};
}

float distanceBetween(Position from, Position to) noexcept {
    return std::hypot(to.x - from.x, to.y - from.y, to.z - from.z);
}

/// What a streamed play decodes before it starts: 40 ms.
constexpr std::size_t kPrimedFrames = 1'920;

/// One instance, as the owner keeps it.
struct Live {
    std::uint32_t generation = 0;
    bool used = false;
    InstanceState state = InstanceState::Finished;
    std::size_t sound = 0;
    std::size_t variant = 0;
    std::optional<Position> at;
    /// Its drawn volume as a gain, and its drawn pitch.
    float gain = 1;
    float pitch = 1;
    /// The order instances started in: lower is older.
    std::uint64_t started = 0;
    std::optional<Playback> playback;
    /// Where in its clip it is, in frames, kept by the owner so a virtual
    /// instance comes back where it would be.
    double frame = 0;
    /// Its gain after distance, last update: what `stop_quietest` weighs.
    float audible = 1;
};

} // namespace

struct Sounds::State {
    Mixer* mixer = nullptr;
    Layout layout;
    SoundsSettings settings;
    SoundsStatistics statistics;
    world::Pcg32 random = world::deriveStream(world::RootSeed{0}, "rawframe.audio", "sounds");
    std::vector<LoadedSound> sounds;
    /// Per sound and variant: its length in frames and its rate.
    std::vector<std::vector<std::pair<std::uint64_t, std::uint32_t>>> lengths;
    /// Per sound: the next variant in sequence, and the last one picked.
    std::vector<std::size_t> nextVariant;
    std::vector<std::optional<std::size_t>> lastVariant;
    /// Per on-demand sound: whether its variants are all in, and whether it
    /// was ever named wanted; the ones named since last taken.
    std::vector<bool> complete;
    std::vector<bool> asked;
    std::vector<std::size_t> wanted;
    /// Per sound: seconds since an instance of it last lived; the on-demand
    /// sounds let go since last taken.
    std::vector<float> idle;
    std::vector<std::size_t> idled;
    std::vector<Live> instances;
    std::uint64_t serial = 0;
    std::optional<Listener> listener;

    [[nodiscard]] Live* liveOf(Instance instance) noexcept {
        if (instance.slot >= instances.size()) {
            return nullptr;
        }
        Live& live = instances[instance.slot];
        return live.used && live.generation == instance.generation ? &live : nullptr;
    }

    std::size_t pickVariant(std::size_t sound) {
        const SoundDeclaration& declaration = sounds[sound].declaration;
        const std::size_t kCount = declaration.variants.size();
        std::size_t picked = 0;
        if (declaration.selection == Selection::Sequential) {
            picked = nextVariant[sound];
            nextVariant[sound] = (picked + 1) % kCount;
        } else {
            const bool kAvoid = declaration.selection == Selection::RandomNoImmediateRepeat && kCount > 1 &&
                                lastVariant[sound].has_value();
            std::uint64_t total = 0;
            for (std::size_t index = 0; index < kCount; ++index) {
                total += kAvoid && index == *lastVariant[sound] ? 0 : declaration.variants[index].weight;
            }
            std::uint64_t draw = random.nextU64() % total;
            for (std::size_t index = 0; index < kCount; ++index) {
                const std::uint64_t kWeight =
                    kAvoid && index == *lastVariant[sound] ? 0 : declaration.variants[index].weight;
                if (draw < kWeight) {
                    picked = index;
                    break;
                }
                draw -= kWeight;
            }
        }
        lastVariant[sound] = picked;
        return picked;
    }

    /// A clip's length in frames and its rate, or why it cannot be one of
    /// the declaration's variants.
    static result::Result<std::pair<std::uint64_t, std::uint32_t>> lengthOf(const SoundDeclaration& declaration,
                                                                            const Clip* clip) {
        const double kLength =
            clip == nullptr || clip->rate == 0 ? 0 : static_cast<double>(clip->frames()) / clip->rate;
        if (kLength == 0 || (declaration.loopEnd && *declaration.loopEnd > kLength)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AudioError::BadPlay,
                          "a loop ends past a variant's end, or a variant is empty");
        }
        return std::pair<std::uint64_t, std::uint32_t>{clip->frames(), clip->rate};
    }

    float draw(float minimum, float maximum) noexcept {
        return minimum + ((maximum - minimum) * random.nextFloat());
    }

    /// Distance gain and pan of a spatial instance now; none for a flat one.
    [[nodiscard]] std::pair<float, float> placement(const Live& live) const noexcept {
        const std::optional<Attenuation>& attenuation = sounds[live.sound].declaration.attenuation;
        if (!attenuation) {
            return {1.0F, 0.0F};
        }
        if (!listener || !live.at) {
            return {attenuation->noListener == NoListener::FlatFallback ? 1.0F : 0.0F, 0.0F};
        }
        const Position kAt = *live.at;
        const Position kFrom = listener->position;
        const float kDistance = distanceBetween(kFrom, kAt);
        const float kRight = listener->right.x * (kAt.x - kFrom.x) + listener->right.y * (kAt.y - kFrom.y) +
                             listener->right.z * (kAt.z - kFrom.z);
        const float kPan = kDistance > 0 ? std::clamp(kRight / kDistance, -1.0F, 1.0F) : 0.0F;
        return {attenuate(*attenuation, kDistance), kPan};
    }

    [[nodiscard]] float distanceOf(const Live& live) const noexcept {
        return listener && live.at ? distanceBetween(listener->position, *live.at) : 0.0F;
    }

    /// Starts a voice for a live instance at its frame; false if none is free.
    bool voice(Live& live, float audible, float pan) {
        const LoadedSound& sound = sounds[live.sound];
        const SoundDeclaration& declaration = sound.declaration;
        PlayParameters parameters{.bus = declaration.bus,
                                  .volume = 20.0F * std::log10(std::max(live.gain * audible, 1e-6F)),
                                  .pitch = live.pitch,
                                  .pan = pan};
        if (declaration.loading == Loading::Stream) {
            // A stream of its own for each play, begun where the instance is
            // and primed with 40 ms so it sounds at once; the streamer keeps
            // it ahead from then on.
            auto stream = Stream::open(sound.cooked[live.variant],
                                       {.bufferFrames = settings.streamBufferFrames,
                                        .loop = declaration.loop,
                                        .startFrame = static_cast<std::uint64_t>(live.frame)});
            if (!stream.has_value()) {
                return false;
            }
            while ((*stream)->available() < kPrimedFrames && !(*stream)->ended()) {
                (*stream)->decodeAhead(1);
            }
            settings.streamer->add(*stream);
            auto playback = mixer->play(std::move(*stream), parameters);
            if (!playback.has_value()) {
                return false;
            }
            live.playback = *playback;
            live.state = InstanceState::Playing;
            return true;
        }
        const Clip& clip = *sound.clips[live.variant];
        parameters.loop = declaration.loop;
        if (declaration.loop && declaration.loopStart) {
            parameters.loopStart = static_cast<std::uint32_t>(*declaration.loopStart * static_cast<float>(clip.rate));
            parameters.loopEnd = static_cast<std::uint32_t>(*declaration.loopEnd * static_cast<float>(clip.rate));
        }
        parameters.startFrame = static_cast<std::uint32_t>(live.frame);
        auto playback = mixer->play(sound.clips[live.variant], parameters);
        if (!playback.has_value()) {
            return false;
        }
        live.playback = *playback;
        live.state = InstanceState::Playing;
        return true;
    }

    void release(Live& live) noexcept {
        live.used = false;
        live.state = InstanceState::Finished;
        live.playback.reset();
    }

    void virtualize(Live& live) {
        if (live.playback) {
            mixer->stop(*live.playback, 0.05F);
            live.playback.reset();
        }
        if (sounds[live.sound].declaration.virtualization == Virtualization::Restart) {
            live.frame = 0;
        }
        live.state = InstanceState::Virtual;
        ++statistics.virtualized;
    }

    /// Advances an instance's own count of frames; false when a sound that
    /// does not loop has run out.
    bool advance(Live& live, float seconds) noexcept {
        const LoadedSound& sound = sounds[live.sound];
        const auto [kFrames, kRate] = lengths[live.sound][live.variant];
        live.frame += static_cast<double>(seconds) * kRate * live.pitch;
        const auto kLength = static_cast<double>(kFrames);
        if (!sound.declaration.loop) {
            return live.frame < kLength;
        }
        const double kStart = sound.declaration.loopStart ? *sound.declaration.loopStart * kRate : 0.0;
        const double kEnd = sound.declaration.loopEnd ? *sound.declaration.loopEnd * kRate : kLength;
        if (live.frame >= kEnd) {
            live.frame = kStart + std::fmod(live.frame - kStart, kEnd - kStart);
        }
        return true;
    }

    /// Makes room in a full concurrency set for a new play, by the set's
    /// rule; false when the newcomer is the one that yields.
    bool makeRoom(std::size_t set, std::size_t sound, std::optional<Position> at) {
        const ConcurrencySet& rule = layout.concurrency[set];
        std::vector<Live*> members;
        for (Live& live : instances) {
            if (live.used && live.state != InstanceState::Stopping &&
                sounds[live.sound].declaration.concurrency == set) {
                members.push_back(&live);
            }
        }
        if (members.size() < rule.maximumInstances) {
            return true;
        }
        // Who yields: a weight a member and the newcomer each have, the
        // largest yielding, and the oldest among equals (the newcomer is the
        // newest, so it yields only when strictly the largest).
        Live newcomer{.sound = sound, .at = at, .started = serial + 1};
        newcomer.audible = placement(newcomer).first;
        const auto kWeight = [this, &rule](const Live& live) -> double {
            switch (rule.resolution) {
            case Resolution::StopFarthestThenOldest:
                return distanceOf(live);
            case Resolution::StopQuietest:
                return -live.audible;
            case Resolution::StopLowestPriorityThenOldest:
                return -static_cast<double>(sounds[live.sound].declaration.priority);
            case Resolution::StopOldest:
            case Resolution::PreventNew:
                return 0;
            }
            return 0;
        };
        if (rule.resolution == Resolution::PreventNew) {
            return false;
        }
        Live* yielding = nullptr;
        for (Live* member : members) {
            if (yielding == nullptr || kWeight(*member) > kWeight(*yielding) ||
                (kWeight(*member) == kWeight(*yielding) && member->started < yielding->started)) {
                yielding = member;
            }
        }
        if (kWeight(newcomer) > kWeight(*yielding)) {
            return false;
        }
        if (yielding->playback) {
            mixer->stop(*yielding->playback);
        }
        release(*yielding);
        ++statistics.evicted;
        return true;
    }
};

Sounds::Sounds(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Sounds::~Sounds() = default;

result::Result<std::unique_ptr<Sounds>>
Sounds::create(Mixer& mixer, const Layout& layout, const SoundsSettings& settings) {
    if (settings.maximumInstances == 0) {
        return refuse(result::ErrorClass::InvalidArgument, AudioError::BadSettings, "room for no instances");
    }
    auto state = std::make_unique<State>();
    state->mixer = &mixer;
    state->layout = layout;
    state->settings = settings;
    state->random = world::deriveStream(world::RootSeed{settings.seed}, "rawframe.audio", "sounds");
    state->instances.resize(settings.maximumInstances);
    return std::unique_ptr<Sounds>{new Sounds{std::move(state)}};
}

result::Result<std::size_t> Sounds::add(LoadedSound sound) {
    const SoundDeclaration& declaration = sound.declaration;
    const bool kStreamed = declaration.loading == Loading::Stream;
    const std::size_t kHeld = kStreamed ? sound.cooked.size() : sound.clips.size();
    if (kHeld != declaration.variants.size() || (kStreamed ? !sound.clips.empty() : !sound.cooked.empty()) ||
        declaration.bus >= state_->layout.buses.size()) {
        return refuse(result::ErrorClass::InvalidArgument,
                      AudioError::BadPlay,
                      "a sound needs a clip a variant, or cooked Opus a variant if it streams");
    }
    if (kStreamed && state_->settings.streamer == nullptr) {
        return refuse(result::ErrorClass::FailedPrecondition, AudioError::BadPlay, "a streamed sound needs a streamer");
    }
    const bool kOnDemand = declaration.loading == Loading::OnDemand;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> lengths;
    bool complete = true;
    for (std::size_t variant = 0; variant < kHeld; ++variant) {
        if (kStreamed) {
            auto stream = Stream::open(sound.cooked[variant]);
            if (!stream.has_value()) {
                return std::unexpected<result::Error>{std::move(stream).error()};
            }
            lengths.emplace_back((*stream)->frames(), (*stream)->rate());
            continue;
        }
        if (kOnDemand && sound.clips[variant] == nullptr) {
            lengths.emplace_back(0, 0);
            complete = false;
            continue;
        }
        RAWFRAME_TRY_ASSIGN(auto length, State::lengthOf(declaration, sound.clips[variant].get()));
        lengths.push_back(length);
    }
    state_->sounds.push_back(std::move(sound));
    state_->lengths.push_back(std::move(lengths));
    state_->nextVariant.push_back(0);
    state_->lastVariant.emplace_back();
    state_->complete.push_back(complete);
    state_->asked.push_back(false);
    state_->idle.push_back(0);
    return state_->sounds.size() - 1;
}

result::Status Sounds::supply(std::size_t sound, std::size_t variant, std::shared_ptr<const Clip> clip) {
    State& state = *state_;
    if (sound >= state.sounds.size() || state.sounds[sound].declaration.loading == Loading::Stream ||
        variant >= state.sounds[sound].clips.size()) {
        return refuse(result::ErrorClass::InvalidArgument,
                      AudioError::BadPlay,
                      "only a preloaded or on-demand sound's own variants are supplied a clip");
    }
    LoadedSound& loaded = state.sounds[sound];
    RAWFRAME_TRY_ASSIGN(state.lengths[sound][variant], State::lengthOf(loaded.declaration, clip.get()));
    loaded.clips[variant] = std::move(clip);
    state.complete[sound] = std::ranges::none_of(loaded.clips, [](const auto& each) {
        return each == nullptr;
    });
    return {};
}

result::Status
Sounds::supplyCooked(std::size_t sound, std::size_t variant, std::shared_ptr<const std::vector<std::byte>> cooked) {
    State& state = *state_;
    if (sound >= state.sounds.size() || state.sounds[sound].declaration.loading != Loading::Stream ||
        variant >= state.sounds[sound].cooked.size()) {
        return refuse(result::ErrorClass::InvalidArgument,
                      AudioError::BadPlay,
                      "only a streamed sound's own variants are supplied cooked bytes");
    }
    auto stream = Stream::open(cooked);
    if (!stream.has_value()) {
        return std::unexpected<result::Error>{std::move(stream).error()};
    }
    state.lengths[sound][variant] = {(*stream)->frames(), (*stream)->rate()};
    state.sounds[sound].cooked[variant] = std::move(cooked);
    return {};
}

std::vector<std::size_t> Sounds::takeWanted() {
    return std::exchange(state_->wanted, {});
}

std::vector<std::size_t> Sounds::takeIdle() {
    return std::exchange(state_->idled, {});
}

result::Result<Instance> Sounds::play(std::size_t sound, std::optional<Position> at) {
    State& state = *state_;
    if (sound >= state.sounds.size()) {
        return refuse(result::ErrorClass::NotFound, AudioError::BadPlay, "no such sound");
    }
    const SoundDeclaration& declaration = state.sounds[sound].declaration;
    if (!state.complete[sound]) {
        if (!state.asked[sound]) {
            state.asked[sound] = true;
            state.wanted.push_back(sound);
        }
        ++state.statistics.notLoaded;
        return refuse(result::ErrorClass::Unavailable,
                      AudioError::NotLoaded,
                      "the on-demand sound's variants are not all in yet");
    }
    if (declaration.concurrency && !state.makeRoom(*declaration.concurrency, sound, at)) {
        ++state.statistics.refusedByConcurrency;
        return refuse(result::ErrorClass::ResourceExhausted,
                      AudioError::Concurrency,
                      "the sound's concurrency set is full and it yields");
    }
    const auto kFree = std::ranges::find(state.instances, false, &Live::used);
    if (kFree == state.instances.end()) {
        return refuse(result::ErrorClass::ResourceExhausted, AudioError::NoVoice, "every instance slot is in use");
    }
    Live& live = *kFree;
    live = Live{.generation = live.generation + 1,
                .used = true,
                .state = InstanceState::Playing,
                .sound = sound,
                .variant = state.pickVariant(sound),
                .at = at,
                .gain = gainOf(state.draw(declaration.volumeMinimum, declaration.volumeMaximum)),
                .pitch = state.draw(declaration.pitchMinimum, declaration.pitchMaximum),
                .started = ++state.serial};
    state.idle[sound] = 0;
    const auto [kAudible, kPan] = state.placement(live);
    live.audible = kAudible;
    const bool kInRange = !declaration.attenuation || kAudible > 0;
    if (!kInRange || !state.voice(live, kAudible, kPan)) {
        if (declaration.virtualization == Virtualization::Disabled) {
            state.release(live);
            return refuse(result::ErrorClass::ResourceExhausted,
                          AudioError::NoVoice,
                          kInRange ? "no voice is free and the sound may not go virtual"
                                   : "the sound is out of range and may not go virtual");
        }
        state.virtualize(live);
    }
    return Instance{.slot = static_cast<std::uint32_t>(kFree - state.instances.begin()), .generation = live.generation};
}

void Sounds::stop(Instance instance, float fade) {
    State& state = *state_;
    Live* live = state.liveOf(instance);
    if (live == nullptr) {
        return;
    }
    if (live->playback) {
        state.mixer->stop(*live->playback, fade);
        live->state = InstanceState::Stopping;
    } else {
        state.release(*live);
    }
}

void Sounds::move(Instance instance, Position at) {
    if (Live* live = state_->liveOf(instance)) {
        live->at = at;
    }
}

void Sounds::setListener(std::optional<Listener> listener) {
    state_->listener = listener;
}

void Sounds::update(float seconds) {
    State& state = *state_;
    state.mixer->collect();
    if (state.settings.streamer != nullptr) {
        state.settings.streamer->update();
    }
    for (Live& live : state.instances) {
        if (!live.used) {
            continue;
        }
        const SoundDeclaration& declaration = state.sounds[live.sound].declaration;
        const bool kRunning = state.advance(live, seconds);
        if (live.playback && state.mixer->state(*live.playback) == PlaybackState::Finished) {
            state.release(live);
            continue;
        }
        if (live.state == InstanceState::Stopping) {
            continue;
        }
        if (live.state == InstanceState::Virtual && !kRunning) {
            state.release(live);
            continue;
        }
        const auto [kAudible, kPan] = state.placement(live);
        live.audible = kAudible;
        const bool kInRange = !declaration.attenuation || kAudible > 0;
        if (live.state == InstanceState::Playing && !kInRange) {
            if (declaration.virtualization == Virtualization::Disabled) {
                state.mixer->stop(*live.playback);
                live.state = InstanceState::Stopping;
                ++state.statistics.culled;
            } else {
                state.virtualize(live);
            }
        } else if (live.state == InstanceState::Playing && live.playback) {
            state.mixer->setVolume(*live.playback, 20.0F * std::log10(std::max(live.gain * kAudible, 1e-6F)));
            state.mixer->setPan(*live.playback, kPan);
        } else if (live.state == InstanceState::Virtual && kInRange && state.voice(live, kAudible, kPan)) {
            ++state.statistics.revived;
        }
    }
    // An on-demand sound nothing has played for long enough is let go: an
    // instance still living, virtual or stopping, keeps it.
    for (std::size_t sound = 0; sound < state.sounds.size(); ++sound) {
        state.idle[sound] += seconds;
    }
    for (const Live& live : state.instances) {
        if (live.used) {
            state.idle[live.sound] = 0;
        }
    }
    for (std::size_t sound = 0; sound < state.sounds.size(); ++sound) {
        LoadedSound& loaded = state.sounds[sound];
        if (loaded.declaration.loading != Loading::OnDemand || !state.complete[sound] ||
            state.idle[sound] < state.settings.onDemandIdleSeconds) {
            continue;
        }
        std::ranges::fill(loaded.clips, nullptr);
        std::ranges::fill(state.lengths[sound], std::pair<std::uint64_t, std::uint32_t>{0, 0});
        state.complete[sound] = false;
        state.asked[sound] = false;
        state.idled.push_back(sound);
        ++state.statistics.idled;
    }
}

InstanceState Sounds::state(Instance instance) const noexcept {
    const State& state = *state_;
    if (instance.slot >= state.instances.size() || state.instances[instance.slot].generation != instance.generation ||
        !state.instances[instance.slot].used) {
        return InstanceState::Finished;
    }
    return state.instances[instance.slot].state;
}

const SoundDeclaration* Sounds::declaration(std::size_t sound) const noexcept {
    return sound < state_->sounds.size() ? &state_->sounds[sound].declaration : nullptr;
}

const SoundsStatistics& Sounds::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::audio
