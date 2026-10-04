#include "rawframe/audio/output.h"

#include "rawframe/audio/errors.h"
#include "rawframe/execution/time.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <maul-audio/context.h>
#include <maul-audio/device.h>
#include <maul-audio/stream.h>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rawframe::audio {

namespace {

constexpr std::uint32_t kChannels = 2;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AudioError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kAudioDomain, code(error), why).error()};
}

std::unexpected<result::Error> noDevice(std::string_view why, maudResult outcome) {
    return std::unexpected<result::Error>{refuse(result::ErrorClass::Unavailable, AudioError::NoDevice, why)
                                              .error()
                                              .withContext("outcome", maudResultName(outcome))};
}

void raise(std::atomic<std::int64_t>& largest, std::int64_t value) noexcept {
    std::int64_t seen = largest.load(std::memory_order_relaxed);
    while (value > seen && !largest.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

std::string_view nameOf(maudBackendKind backend) noexcept {
    switch (backend) {
    case maud_backendOffline:
        return "Null";
    case maud_backendPipewire:
        return "PipeWire";
    case maud_backendPulse:
        return "PulseAudio";
    case maud_backendAlsa:
        return "ALSA";
    case maud_backendWasapi:
        return "WASAPI";
    case maud_backendCoreAudio:
        return "Core Audio";
    case maud_backendAaudio:
        return "AAudio";
    case maud_backendWeb:
        return "Web Audio";
    default:
        return "native";
    }
}

/// What the device's thread and the owner share.
struct Shared {
    execution::SteadyClock clock;
    /// The mixer the device's thread renders, or none.
    std::atomic<Mixer*> mixer{nullptr};
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<std::uint64_t> frames{0};
    std::atomic<std::int64_t> largestCallbackFrames{0};
    std::atomic<std::int64_t> longestCallbackNanoseconds{0};
};

// The device's thread: renders the mixer into the block it asks for. The
// block arrives silent, so a missing mixer leaves silence.
void render(const maudStreamBlock* block, void* user) {
    auto& shared = *static_cast<Shared*>(user);
    shared.callbacks.fetch_add(1, std::memory_order_relaxed);
    shared.frames.fetch_add(block->frameCount, std::memory_order_relaxed);
    raise(shared.largestCallbackFrames, block->frameCount);
    Mixer* const kMixer = shared.mixer.load(std::memory_order_acquire);
    if (kMixer == nullptr || block->output == nullptr) {
        return;
    }
    const execution::MonotonicInstant kBegan = shared.clock.now();
    kMixer->render(std::span{block->output, static_cast<std::size_t>(block->frameCount) * kChannels});
    const execution::MonotonicInstant kEnded = shared.clock.now();
    raise(shared.longestCallbackNanoseconds, (kEnded - kBegan).nanoseconds);
}

} // namespace

struct Output::State {
    maudContext* context = nullptr;
    maudStreamId stream{};
    maudBackendKind backend = maud_backendOffline;
    std::uint32_t rate = 0;
    std::uint32_t period = 0;
    bool rendering = false;
    bool lost = false;
    Shared shared;
    /// The null device's thread: renders the offline stream a period at a
    /// time at the wall clock's pace, as a device would ask for it.
    std::thread pacer;
    std::atomic<bool> pacing{false};

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        stopPacing();
        if (stream.index1 != 0) {
            static_cast<void>(maudDestroyStream(context, stream));
        }
        static_cast<void>(maudDestroyContext(context));
    }

    void startPacing() {
        pacing.store(true, std::memory_order_release);
        pacer = std::thread([this] {
            std::vector<float> buffer(static_cast<std::size_t>(period) * kChannels);
            const auto kPeriod = std::chrono::nanoseconds{std::int64_t{1'000'000'000} * period / rate};
            auto next = std::chrono::steady_clock::now();
            while (pacing.load(std::memory_order_acquire)) {
                static_cast<void>(maudRenderStream(context, stream, buffer.data(), period));
                next += kPeriod;
                std::this_thread::sleep_until(next);
            }
        });
    }

    void stopPacing() noexcept {
        pacing.store(false, std::memory_order_release);
        if (pacer.joinable()) {
            pacer.join();
        }
    }
};

Output::Output(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Output::~Output() {
    stop();
}

result::Result<std::unique_ptr<Output>> Output::open(const OutputSettings& settings) {
    if (settings.periodFrames == 0 || (settings.rate != 0 && (settings.rate < 8'000 || settings.rate > 192'000))) {
        return refuse(result::ErrorClass::InvalidArgument,
                      AudioError::BadSettings,
                      "an output needs a period, and a rate of 8 to 192 kHz or the device's own");
    }
    auto state = std::make_unique<State>();
    const bool kNull = settings.backend == OutputBackend::Null;

    // The platform's best backend, never a silent one it did not ask for: a
    // machine without sound has no device. The null device is Maul Audio's
    // offline backend, rendered on a timer.
    maudContextDef contextDef = maudDefaultContextDef();
    contextDef.backend = kNull ? maud_backendOffline : maud_backendNative;
    contextDef.limits.periodFrames = std::max(contextDef.limits.periodFrames, settings.periodFrames);
    if (kNull) {
        contextDef.offlineSampleRate = settings.rate != 0 ? settings.rate : 48'000;
    }
    if (const maudResult kMade = maudCreateContext(&contextDef, &state->context); kMade != maud_success) {
        return noDevice("no audio backend is available", kMade);
    }
    state->backend = maudGetContextBackend(state->context);
    maudDeviceId device{};
    if (const maudResult kFound = maudGetDefaultDevice(state->context, maud_directionOutput, maud_roleGeneral, &device);
        kFound != maud_success || device.index1 == 0) {
        return std::unexpected<result::Error>{noDevice("no output device could be opened", kFound)
                                                  .error()
                                                  .withContext("backend", std::string{nameOf(state->backend)})};
    }

    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.direction = maud_directionOutput;
    // The offline backend is pulled by the null device's thread; a device's
    // calls back from its own.
    streamDef.mode = kNull ? maud_modePull : maud_modeCallback;
    streamDef.layout = maud_layoutStereo;
    streamDef.ratePolicy = settings.rate != 0 && !kNull ? maud_ratePlatformConverted : maud_rateNative;
    streamDef.sampleRate = settings.rate != 0 && !kNull ? settings.rate : 0;
    streamDef.periodFrames = settings.periodFrames;
    streamDef.callback = &render;
    streamDef.user = &state->shared;
    if (const maudResult kMade = maudCreateStream(state->context, &streamDef, &state->stream); kMade != maud_success) {
        return std::unexpected<result::Error>{noDevice("no output device could be opened", kMade)
                                                  .error()
                                                  .withContext("backend", std::string{nameOf(state->backend)})};
    }
    maudStreamFormat format{};
    if (const maudResult kRead = maudGetStreamFormat(state->context, state->stream, &format); kRead != maud_success) {
        return noDevice("the output device's format could not be read", kRead);
    }
    state->rate = format.sampleRate;
    state->period = format.periodFrames != 0 ? format.periodFrames : settings.periodFrames;
    return std::unique_ptr<Output>{new Output{std::move(state)}};
}

std::uint32_t Output::rate() const noexcept {
    return state_->rate;
}

std::string Output::backendName() const {
    return std::string{nameOf(state_->backend)};
}

result::Status Output::start(Mixer& mixer) {
    if (mixer.rate() != rate()) {
        return std::unexpected<result::Error>{
            refuse(result::ErrorClass::InvalidArgument, AudioError::BadSettings, "the mixer renders at another rate")
                .error()
                .withContext("mixer", std::to_string(mixer.rate()))
                .withContext("device", std::to_string(rate()))};
    }
    if (state_->rendering) {
        return refuse(result::ErrorClass::FailedPrecondition, AudioError::BadSettings, "the output is rendering");
    }
    state_->shared.mixer.store(&mixer, std::memory_order_release);
    if (const maudResult kStarted = maudStartStream(state_->context, state_->stream); kStarted != maud_success) {
        state_->shared.mixer.store(nullptr, std::memory_order_release);
        state_->lost = true;
        return noDevice("the output device would not start", kStarted);
    }
    state_->rendering = true;
    state_->lost = false;
    if (state_->backend == maud_backendOffline) {
        state_->startPacing();
    }
    return {};
}

void Output::stop() noexcept {
    state_->stopPacing();
    if (state_->rendering) {
        // Returns once the device's thread has left its last callback.
        static_cast<void>(maudStopStream(state_->context, state_->stream));
        state_->rendering = false;
    }
    state_->shared.mixer.store(nullptr, std::memory_order_release);
}

OutputState Output::state() const noexcept {
    if (!state_->rendering) {
        return state_->lost ? OutputState::Lost : OutputState::Stopped;
    }
    // A device that went away, or a default that left no device, suspends
    // the stream: output is lost, and the mixer is not rendered.
    maudStreamStatus status{};
    if (maudGetStreamStatus(state_->context, state_->stream, &status) == maud_success &&
        (status.suspension == maud_suspendDeviceLost || status.suspension == maud_suspendNoDevice)) {
        return OutputState::Lost;
    }
    return OutputState::Rendering;
}

OutputStatistics Output::statistics() const noexcept {
    return OutputStatistics{
        .callbacks = state_->shared.callbacks.load(std::memory_order_relaxed),
        .frames = state_->shared.frames.load(std::memory_order_relaxed),
        .largestCallbackFrames = static_cast<std::uint32_t>(
            std::min<std::int64_t>(state_->shared.largestCallbackFrames.load(std::memory_order_relaxed), UINT32_MAX)),
        .longestCallbackNanoseconds = state_->shared.longestCallbackNanoseconds.load(std::memory_order_relaxed),
    };
}

} // namespace rawframe::audio
