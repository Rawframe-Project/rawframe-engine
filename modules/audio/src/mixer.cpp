#include "rawframe/audio/mixer.h"

#include "effects.h"
#include "rawframe/audio/errors.h"
#include "rawframe/audio/stream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <numbers>
#include <tuple>
#include <utility>

namespace rawframe::audio {

namespace {

/// A bounded queue from one thread to one other, allocated once. Neither
/// side waits: a push to a full queue and a pop from an empty one say so.
template <typename T> class Ring {
public:
    explicit Ring(std::size_t capacity) : slots_(std::bit_ceil(std::max<std::size_t>(capacity, 2))) {
    }

    bool push(const T& value) noexcept {
        const std::size_t kHead = head_.load(std::memory_order_relaxed);
        if (kHead - tail_.load(std::memory_order_acquire) == slots_.size()) {
            return false;
        }
        slots_[kHead & (slots_.size() - 1)] = value;
        head_.store(kHead + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept {
        const std::size_t kTail = tail_.load(std::memory_order_relaxed);
        if (kTail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        value = slots_[kTail & (slots_.size() - 1)];
        tail_.store(kTail + 1, std::memory_order_release);
        return true;
    }

private:
    std::vector<T> slots_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

struct Command {
    enum class Kind : std::uint8_t {
        Play,
        Stop,
        Volume,
        Pitch,
        Pan,
        BusVolume,
        BusMuted,
        EffectParameter
    };
    Kind kind = Kind::Play;
    std::uint32_t voice = 0;
    std::uint32_t generation = 0;
    const Clip* clip = nullptr;
    Stream* stream = nullptr;
    std::uint32_t bus = 0;
    /// Play and Volume: linear gain; Pitch: the ratio; Stop: fade frames;
    /// BusVolume: linear gain; BusMuted: nonzero for muted.
    float value = 0;
    float pitch = 1;
    float pan = 0;
    bool loop = false;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
    std::uint32_t startFrame = 0;
    /// EffectParameter: which effect of the bus, which band, and what.
    std::uint32_t effect = 0;
    std::uint32_t band = 0;
    audio::EffectParameter parameter = audio::EffectParameter::Bypass;
};

struct Finished {
    std::uint32_t voice = 0;
    std::uint32_t generation = 0;
};

/// A pan's gains, left and right: constant power for one channel, balance
/// for two.
std::pair<float, float> panGains(std::uint32_t channels, float pan) noexcept {
    if (channels == 1) {
        const float kAngle = (pan + 1.0F) * std::numbers::pi_v<float> / 4.0F;
        return {std::cos(kAngle), std::sin(kAngle)};
    }
    return {std::min(1.0F, 1.0F - pan), std::min(1.0F, 1.0F + pan)};
}

/// The owner's view of one voice.
struct Slot {
    std::shared_ptr<const Clip> clip;
    std::shared_ptr<Stream> stream;
    std::uint32_t generation = 0;
    PlaybackState state = PlaybackState::Finished;
    bool used = false;
};

/// The mix thread's view of one voice.
struct Voice {
    bool active = false;
    std::uint32_t generation = 0;
    const Clip* clip = nullptr;
    Stream* stream = nullptr;
    std::uint32_t channels = 1;
    std::uint32_t bus = 0;
    double position = 0;
    float pitch = 1;
    float gain = 1;
    float currentGain = 0;
    float left = 1;
    float right = 1;
    bool loop = false;
    /// The loop's region, in frames.
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
    bool stopping = false;
    /// While stopping: the fade's level and how much it falls a frame.
    float fade = 1;
    float fadeStep = 0;
};

struct BusRuntime {
    std::vector<float> buffer;
    std::vector<EffectRuntime> effects;
    std::size_t parent = 0;
    std::vector<Send> sends;
    std::vector<float> sendGains;
    float fader = 1;
    float currentFader = 1;
    bool muted = false;
};

} // namespace

struct Mixer::State {
    MixerSettings settings;
    Layout layout;
    std::vector<std::size_t> order;
    Ring<Command> commands;
    Ring<Finished> finished;
    MixerStatistics statistics;
    // The owner's.
    std::vector<Slot> slots;
    // The mix thread's.
    std::vector<Voice> voices;
    std::vector<BusRuntime> buses;
    /// Four a bus: peaks, then root mean squares, left and right.
    std::unique_ptr<std::atomic<float>[]> meters;

    State(const Layout& made, const MixerSettings& chosen)
        : settings(chosen), layout(made), order(made.mixOrder()), commands(chosen.commandQueue),
          finished(chosen.voices), slots(chosen.voices), voices(chosen.voices),
          meters(std::make_unique<std::atomic<float>[]>(made.buses.size() * 4)) {
        const std::size_t kSamples = static_cast<std::size_t>(settings.blockFrames) * 2;
        for (const Bus& bus : layout.buses) {
            BusRuntime runtime;
            runtime.buffer.assign(kSamples, 0.0F);
            runtime.parent = bus.parent;
            runtime.sends = bus.sends;
            for (const Send& send : bus.sends) {
                runtime.sendGains.push_back(gainOf(send.level));
            }
            runtime.fader = gainOf(bus.volume);
            runtime.currentFader = runtime.fader;
            runtime.muted = bus.muted;
            for (const Effect& effect : bus.effects) {
                runtime.effects.emplace_back(effect, settings.rate, settings.shortestFade);
            }
            buses.push_back(std::move(runtime));
        }
    }

    [[nodiscard]] bool send(const Command& command) {
        if (!commands.push(command)) {
            ++statistics.queueFull;
            return false;
        }
        return true;
    }

    [[nodiscard]] Slot* slotOf(Playback playback) noexcept {
        if (playback.voice >= slots.size()) {
            return nullptr;
        }
        Slot& slot = slots[playback.voice];
        return slot.used && slot.generation == playback.generation ? &slot : nullptr;
    }

    // The mix thread from here on.

    void apply(const Command& command) noexcept {
        switch (command.kind) {
        case Command::Kind::Play: {
            Voice& voice = voices[command.voice];
            const std::uint32_t kChannels =
                command.stream != nullptr ? command.stream->channels() : command.clip->channels;
            const auto [kLeft, kRight] = panGains(kChannels, command.pan);
            voice = Voice{.active = true,
                          .generation = command.generation,
                          .clip = command.clip,
                          .stream = command.stream,
                          .channels = kChannels,
                          .bus = command.bus,
                          .position = static_cast<double>(command.startFrame),
                          .pitch = command.pitch,
                          .gain = command.value,
                          .currentGain = command.value,
                          .left = kLeft,
                          .right = kRight,
                          .loop = command.loop,
                          .loopStart = command.loopStart,
                          .loopEnd = command.loopEnd != 0 || command.clip == nullptr
                                         ? command.loopEnd
                                         : static_cast<std::uint32_t>(command.clip->frames())};
            break;
        }
        case Command::Kind::Stop: {
            Voice& voice = voices[command.voice];
            if (voice.active && voice.generation == command.generation && !voice.stopping) {
                voice.stopping = true;
                voice.fadeStep = 1.0F / std::max(command.value, 1.0F);
            }
            break;
        }
        case Command::Kind::Volume:
        case Command::Kind::Pitch: {
            Voice& voice = voices[command.voice];
            if (voice.active && voice.generation == command.generation) {
                (command.kind == Command::Kind::Volume ? voice.gain : voice.pitch) = command.value;
            }
            break;
        }
        case Command::Kind::Pan: {
            Voice& voice = voices[command.voice];
            if (voice.active && voice.generation == command.generation) {
                std::tie(voice.left, voice.right) = panGains(voice.channels, command.pan);
            }
            break;
        }
        case Command::Kind::BusVolume:
            buses[command.bus].fader = command.value;
            break;
        case Command::Kind::BusMuted:
            buses[command.bus].muted = command.value != 0;
            break;
        case Command::Kind::EffectParameter:
            buses[command.bus].effects[command.effect].set(command.parameter, command.band, command.value);
            break;
        }
    }

    /// One voice into its bus for `frames` frames.
    void mixVoice(Voice& voice, std::size_t frames) noexcept {
        if (voice.stream != nullptr) {
            mixStream(voice, frames);
            return;
        }
        const Clip& clip = *voice.clip;
        const std::size_t kLength = clip.frames();
        const double kStep = voice.pitch * static_cast<double>(clip.rate) / static_cast<double>(settings.rate);
        float* const out = buses[voice.bus].buffer.data();
        const float kFrom = voice.currentGain;
        const float kRamp = (voice.gain - kFrom) / static_cast<float>(frames);
        const std::size_t kSpan = voice.loopEnd - voice.loopStart;
        const auto kSample = [&clip, kLength, kSpan, &voice](std::size_t frame, std::size_t channel) {
            if (voice.loop && frame >= voice.loopEnd) {
                frame = voice.loopStart + ((frame - voice.loopStart) % kSpan);
            } else if (frame >= kLength) {
                return 0.0F;
            }
            return clip.samples[(frame * clip.channels) + std::min<std::size_t>(channel, clip.channels - 1)];
        };
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (voice.loop && voice.position >= static_cast<double>(voice.loopEnd)) {
                const double kOver = voice.position - static_cast<double>(voice.loopStart);
                voice.position =
                    static_cast<double>(voice.loopStart) +
                    (kOver - (static_cast<double>(kSpan) * std::floor(kOver / static_cast<double>(kSpan))));
            } else if (voice.position >= static_cast<double>(kLength)) {
                finish(voice);
                return;
            }
            if (voice.stopping) {
                voice.fade -= voice.fadeStep;
                if (voice.fade <= 0) {
                    finish(voice);
                    return;
                }
            }
            const auto kWhole = static_cast<std::size_t>(voice.position);
            const auto kPart = static_cast<float>(voice.position - static_cast<double>(kWhole));
            const float kGain = (kFrom + (kRamp * static_cast<float>(frame))) * voice.fade;
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const float kA = kSample(kWhole, channel);
                const float kB = kSample(kWhole + 1, channel);
                const float kValue = (kA + ((kB - kA) * kPart)) * kGain * (channel == 0 ? voice.left : voice.right);
                out[(frame * 2) + channel] += kValue;
            }
            voice.position += kStep;
        }
        voice.currentGain = voice.gain;
    }

    /// One streaming voice into its bus: its position counts from the
    /// stream's next frame, and what it passes is consumed. A ring run dry
    /// holds the voice where it is, silent, and counts each frame.
    void mixStream(Voice& voice, std::size_t frames) noexcept {
        Stream& stream = *voice.stream;
        const double kStep = voice.pitch * static_cast<double>(stream.rate()) / static_cast<double>(settings.rate);
        float* const out = buses[voice.bus].buffer.data();
        const float kFrom = voice.currentGain;
        const float kRamp = (voice.gain - kFrom) / static_cast<float>(frames);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto kWhole = static_cast<std::size_t>(voice.position);
            const bool kEnded = stream.ended();
            const std::size_t kAvailable = stream.available();
            if (kAvailable < kWhole + 2) {
                if (kEnded && kAvailable <= kWhole) {
                    finish(voice);
                    return;
                }
                if (!kEnded) {
                    stream.countUnderrun();
                    continue;
                }
            }
            if (voice.stopping) {
                voice.fade -= voice.fadeStep;
                if (voice.fade <= 0) {
                    finish(voice);
                    return;
                }
            }
            const auto kPart = static_cast<float>(voice.position - static_cast<double>(kWhole));
            const float kGain = (kFrom + (kRamp * static_cast<float>(frame))) * voice.fade;
            const bool kLast = kAvailable < kWhole + 2;
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                const float kA = stream.sample(kWhole, channel);
                const float kB = kLast ? 0.0F : stream.sample(kWhole + 1, channel);
                out[(frame * 2) + channel] +=
                    (kA + ((kB - kA) * kPart)) * kGain * (channel == 0 ? voice.left : voice.right);
            }
            voice.position += kStep;
            const auto kPassed = std::min(static_cast<std::size_t>(voice.position), kAvailable);
            stream.consume(kPassed);
            voice.position -= static_cast<double>(kPassed);
        }
        voice.currentGain = voice.gain;
    }

    void finish(Voice& voice) noexcept {
        voice.active = false;
        // Never full: a voice finishes once a use, and is not reused before
        // the owner collects it.
        static_cast<void>(finished.push(
            Finished{.voice = static_cast<std::uint32_t>(&voice - voices.data()), .generation = voice.generation}));
    }

    void addInto(std::vector<float>& target, const float* from, std::size_t frames, float gain) noexcept {
        for (std::size_t index = 0; index < frames * 2; ++index) {
            target[index] += from[index] * gain;
        }
    }

    void renderBlock(float* output, std::size_t frames) noexcept {
        Command command;
        while (commands.pop(command)) {
            apply(command);
        }
        for (BusRuntime& bus : buses) {
            std::fill_n(bus.buffer.begin(), frames * 2, 0.0F);
        }
        for (Voice& voice : voices) {
            if (voice.active) {
                mixVoice(voice, frames);
            }
        }
        for (const std::size_t kIndex : order) {
            BusRuntime& bus = buses[kIndex];
            float* const buffer = bus.buffer.data();
            for (EffectRuntime& effect : bus.effects) {
                // A key is mixed before the bus it keys (Layout::mixOrder).
                const std::optional<std::size_t> kKey = effect.key();
                effect.run(buffer, kKey ? buses[*kKey].buffer.data() : nullptr, frames);
            }
            for (std::size_t send = 0; send < bus.sends.size(); ++send) {
                if (bus.sends[send].position == SendPosition::PreFader) {
                    addInto(buses[bus.sends[send].target].buffer, buffer, frames, bus.sendGains[send]);
                }
            }
            // The fader moves across the block, never in one step.
            const float kTarget = bus.muted ? 0.0F : bus.fader;
            const float kRamp = (kTarget - bus.currentFader) / static_cast<float>(frames);
            std::array<float, 2> peak{};
            std::array<float, 2> power{};
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const float kGain = bus.currentFader + (kRamp * static_cast<float>(frame));
                for (std::size_t channel = 0; channel < 2; ++channel) {
                    float& sample = buffer[(frame * 2) + channel];
                    sample *= kGain;
                    peak[channel] = std::max(peak[channel], std::abs(sample));
                    power[channel] += sample * sample;
                }
            }
            bus.currentFader = kTarget;
            for (std::size_t send = 0; send < bus.sends.size(); ++send) {
                if (bus.sends[send].position == SendPosition::PostFader) {
                    addInto(buses[bus.sends[send].target].buffer, buffer, frames, bus.sendGains[send]);
                }
            }
            if (bus.parent != kIndex) {
                addInto(buses[bus.parent].buffer, buffer, frames, 1.0F);
            }
            std::atomic<float>* const meter = &meters[kIndex * 4];
            meter[0].store(peak[0], std::memory_order_relaxed);
            meter[1].store(peak[1], std::memory_order_relaxed);
            meter[2].store(std::sqrt(power[0] / static_cast<float>(frames)), std::memory_order_relaxed);
            meter[3].store(std::sqrt(power[1] / static_cast<float>(frames)), std::memory_order_relaxed);
        }
        std::copy_n(buses[0].buffer.begin(), frames * 2, output);
    }
};

Mixer::Mixer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Mixer::~Mixer() = default;

result::Result<std::unique_ptr<Mixer>> Mixer::create(const Layout& layout, const MixerSettings& settings) {
    if (layout.buses.empty() || settings.rate < 8'000 || settings.rate > 192'000 || settings.blockFrames == 0 ||
        settings.voices == 0 || settings.commandQueue == 0) {
        return result::fail(
            result::ErrorClass::InvalidArgument,
            kAudioDomain,
            code(AudioError::BadSettings),
            "a mixer needs a layout, a rate of 8 to 192 kHz, and room for blocks, voices, and commands");
    }
    return std::unique_ptr<Mixer>{new Mixer{std::make_unique<State>(layout, settings)}};
}

result::Result<Playback> Mixer::play(std::shared_ptr<const Clip> clip, const PlayParameters& parameters) {
    State& state = *state_;
    const std::size_t kFrames = clip == nullptr ? 0 : clip->frames();
    const std::size_t kLoopEnd = parameters.loopEnd == 0 ? kFrames : parameters.loopEnd;
    if (clip == nullptr || clip->channels == 0 || clip->channels > 2 || clip->rate == 0 || kFrames == 0 ||
        kFrames > 0xFFFF'FFFFU || parameters.bus >= state.layout.buses.size() || !(parameters.pitch > 0) ||
        !std::isfinite(parameters.pitch) || parameters.startFrame >= kFrames ||
        (parameters.loop && (parameters.loopStart >= kLoopEnd || kLoopEnd > kFrames))) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kAudioDomain,
                            code(AudioError::BadPlay),
                            "a play needs a clip of one or two channels, a bus of the layout, a pitch above nought, "
                            "and a loop inside the clip");
    }
    const auto kFree = std::ranges::find(state.slots, false, &Slot::used);
    if (kFree == state.slots.end()) {
        ++state.statistics.voicesExhausted;
        return result::fail(
            result::ErrorClass::ResourceExhausted, kAudioDomain, code(AudioError::NoVoice), "every voice is in use");
    }
    const auto kVoice = static_cast<std::uint32_t>(kFree - state.slots.begin());
    const std::uint32_t kGeneration = kFree->generation + 1;
    const Command kPlay{.kind = Command::Kind::Play,
                        .voice = kVoice,
                        .generation = kGeneration,
                        .clip = clip.get(),
                        .bus = static_cast<std::uint32_t>(parameters.bus),
                        .value = gainOf(parameters.volume),
                        .pitch = parameters.pitch,
                        .pan = std::clamp(parameters.pan, -1.0F, 1.0F),
                        .loop = parameters.loop,
                        .loopStart = parameters.loopStart,
                        .loopEnd = parameters.loopEnd,
                        .startFrame = parameters.startFrame};
    if (!state.send(kPlay)) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kAudioDomain,
                            code(AudioError::QueueFull),
                            "the mix thread's command queue is full");
    }
    *kFree = Slot{.clip = std::move(clip),
                  .stream = nullptr,
                  .generation = kGeneration,
                  .state = PlaybackState::Playing,
                  .used = true};
    return Playback{.voice = kVoice, .generation = kGeneration};
}

result::Result<Playback> Mixer::play(std::shared_ptr<Stream> stream, const PlayParameters& parameters) {
    State& state = *state_;
    if (stream == nullptr || parameters.bus >= state.layout.buses.size() || !(parameters.pitch > 0) ||
        !std::isfinite(parameters.pitch) || parameters.loop || parameters.startFrame != 0) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kAudioDomain,
                            code(AudioError::BadPlay),
                            "a stream's play needs a bus of the layout and a pitch above nought, and no loop or "
                            "start frame of its own");
    }
    const auto kFree = std::ranges::find(state.slots, false, &Slot::used);
    if (kFree == state.slots.end()) {
        ++state.statistics.voicesExhausted;
        return result::fail(
            result::ErrorClass::ResourceExhausted, kAudioDomain, code(AudioError::NoVoice), "every voice is in use");
    }
    if (!stream->claimPlay()) {
        return result::fail(result::ErrorClass::FailedPrecondition,
                            kAudioDomain,
                            code(AudioError::BadPlay),
                            "the stream is already playing");
    }
    const auto kVoice = static_cast<std::uint32_t>(kFree - state.slots.begin());
    const std::uint32_t kGeneration = kFree->generation + 1;
    if (!state.send(Command{.kind = Command::Kind::Play,
                            .voice = kVoice,
                            .generation = kGeneration,
                            .stream = stream.get(),
                            .bus = static_cast<std::uint32_t>(parameters.bus),
                            .value = gainOf(parameters.volume),
                            .pitch = parameters.pitch,
                            .pan = std::clamp(parameters.pan, -1.0F, 1.0F)})) {
        stream->releasePlay();
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kAudioDomain,
                            code(AudioError::QueueFull),
                            "the mix thread's command queue is full");
    }
    *kFree = Slot{.clip = nullptr,
                  .stream = std::move(stream),
                  .generation = kGeneration,
                  .state = PlaybackState::Playing,
                  .used = true};
    return Playback{.voice = kVoice, .generation = kGeneration};
}

void Mixer::stop(Playback playback, float fade) {
    State& state = *state_;
    Slot* slot = state.slotOf(playback);
    if (slot == nullptr || slot->state != PlaybackState::Playing) {
        return;
    }
    const float kFrames = std::max(fade, state.settings.shortestFade) * static_cast<float>(state.settings.rate);
    if (state.send(Command{.kind = Command::Kind::Stop,
                           .voice = playback.voice,
                           .generation = playback.generation,
                           .value = kFrames})) {
        slot->state = PlaybackState::Stopping;
    }
}

void Mixer::setVolume(Playback playback, float decibels) {
    State& state = *state_;
    if (state.slotOf(playback) != nullptr) {
        static_cast<void>(state.send(Command{.kind = Command::Kind::Volume,
                                             .voice = playback.voice,
                                             .generation = playback.generation,
                                             .value = gainOf(decibels)}));
    }
}

void Mixer::setPitch(Playback playback, float pitch) {
    State& state = *state_;
    if (state.slotOf(playback) != nullptr && pitch > 0 && std::isfinite(pitch)) {
        static_cast<void>(state.send(Command{
            .kind = Command::Kind::Pitch, .voice = playback.voice, .generation = playback.generation, .value = pitch}));
    }
}

void Mixer::setPan(Playback playback, float pan) {
    State& state = *state_;
    if (state.slotOf(playback) != nullptr && std::isfinite(pan)) {
        static_cast<void>(state.send(Command{.kind = Command::Kind::Pan,
                                             .voice = playback.voice,
                                             .generation = playback.generation,
                                             .pan = std::clamp(pan, -1.0F, 1.0F)}));
    }
}

void Mixer::setBusVolume(std::size_t bus, float decibels) {
    State& state = *state_;
    if (bus < state.layout.buses.size()) {
        static_cast<void>(state.send(Command{
            .kind = Command::Kind::BusVolume, .bus = static_cast<std::uint32_t>(bus), .value = gainOf(decibels)}));
    }
}

void Mixer::setBusMuted(std::size_t bus, bool muted) {
    State& state = *state_;
    if (bus < state.layout.buses.size()) {
        static_cast<void>(state.send(Command{
            .kind = Command::Kind::BusMuted, .bus = static_cast<std::uint32_t>(bus), .value = muted ? 1.0F : 0.0F}));
    }
}

result::Status Mixer::setEffectParameter(
    std::size_t bus, std::size_t effect, EffectParameter parameter, float value, std::size_t band) {
    State& state = *state_;
    const auto kRefuse = [](std::string_view why) {
        return result::fail(result::ErrorClass::InvalidArgument, kAudioDomain, code(AudioError::BadParameter), why);
    };
    if (bus >= state.layout.buses.size() || effect >= state.layout.buses[bus].effects.size()) {
        return kRefuse("no such bus or effect");
    }
    const Effect& written = state.layout.buses[bus].effects[effect];
    const std::optional<ParameterRange> kRange = rangeOf(written.type, parameter);
    if (!kRange.has_value()) {
        return kRefuse("the effect has no such parameter, or it is fixed while it plays");
    }
    if (!std::isfinite(value) || value < kRange->lowest || value > kRange->highest) {
        return kRefuse("the value is outside the parameter's range");
    }
    const bool kBand = parameter == EffectParameter::BandFrequency || parameter == EffectParameter::BandGain ||
                       parameter == EffectParameter::BandQ;
    if (kBand && band >= written.bands.size()) {
        return kRefuse("the equalizer has no such band");
    }
    if (parameter == EffectParameter::BandGain && written.bands[band].shape == BandShape::Notch) {
        return kRefuse("a notch has no gain");
    }
    if (parameter == EffectParameter::Ratio &&
        (written.dynamics.processor == Processor::Limiter || written.dynamics.processor == Processor::Gate)) {
        return kRefuse("a limiter and a gate have no ratio");
    }
    if (!state.send(Command{.kind = Command::Kind::EffectParameter,
                            .bus = static_cast<std::uint32_t>(bus),
                            .value = value,
                            .effect = static_cast<std::uint32_t>(effect),
                            .band = static_cast<std::uint32_t>(kBand ? band : 0),
                            .parameter = parameter})) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kAudioDomain,
                            code(AudioError::QueueFull),
                            "the mix thread's command queue is full");
    }
    return {};
}

void Mixer::collect() {
    State& state = *state_;
    Finished done;
    while (state.finished.pop(done)) {
        Slot& slot = state.slots[done.voice];
        if (slot.generation == done.generation) {
            // The clip or stream is released here, on the owner's thread.
            if (slot.stream != nullptr) {
                slot.stream->releasePlay();
            }
            slot = Slot{.clip = nullptr,
                        .stream = nullptr,
                        .generation = slot.generation,
                        .state = PlaybackState::Finished,
                        .used = false};
        }
    }
}

PlaybackState Mixer::state(Playback playback) const noexcept {
    const State& state = *state_;
    if (playback.voice >= state.slots.size() || state.slots[playback.voice].generation != playback.generation) {
        return PlaybackState::Finished;
    }
    return state.slots[playback.voice].state;
}

BusMeter Mixer::meter(std::size_t bus) const noexcept {
    const State& state = *state_;
    if (bus >= state.layout.buses.size()) {
        return {};
    }
    const std::atomic<float>* const kMeter = &state.meters[bus * 4];
    return BusMeter{.peakLeft = kMeter[0].load(std::memory_order_relaxed),
                    .peakRight = kMeter[1].load(std::memory_order_relaxed),
                    .rmsLeft = kMeter[2].load(std::memory_order_relaxed),
                    .rmsRight = kMeter[3].load(std::memory_order_relaxed)};
}

const MixerStatistics& Mixer::statistics() const noexcept {
    return state_->statistics;
}

std::uint32_t Mixer::rate() const noexcept {
    return state_->settings.rate;
}

void Mixer::render(std::span<float> output) noexcept {
    State& state = *state_;
    const std::size_t kFrames = output.size() / 2;
    for (std::size_t done = 0; done < kFrames;) {
        const std::size_t kStep = std::min<std::size_t>(kFrames - done, state.settings.blockFrames);
        state.renderBlock(output.data() + (done * 2), kStep);
        done += kStep;
    }
}

} // namespace rawframe::audio
