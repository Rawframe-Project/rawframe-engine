#include "rawframe/audio/output.h"

#include "rawframe/audio/errors.h"
#include "rawframe/execution/time.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <miniaudio.h>
#include <span>
#include <string>
#include <utility>

namespace rawframe::audio {

namespace {

constexpr ma_uint32 kChannels = 2;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AudioError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kAudioDomain, code(error), why).error()};
}

void raise(std::atomic<std::int64_t>& largest, std::int64_t value) noexcept {
    std::int64_t seen = largest.load(std::memory_order_relaxed);
    while (value > seen && !largest.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

/// What the device's thread and the owner share.
struct Shared {
    execution::SteadyClock clock;
    /// The mixer the device's thread renders, or none.
    std::atomic<Mixer*> mixer{nullptr};
    std::atomic<OutputState> state{OutputState::Stopped};
    /// Set while the owner stops the device, so its stopping is not a loss.
    std::atomic<bool> stopping{false};
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<std::uint64_t> frames{0};
    std::atomic<std::int64_t> largestCallbackFrames{0};
    std::atomic<std::int64_t> longestCallbackNanoseconds{0};
};

// The device's thread: renders the mixer into the buffer it asks for. The
// buffer arrives silent, so a missing mixer leaves silence.
void render(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount) {
    auto& shared = *static_cast<Shared*>(device->pUserData);
    shared.callbacks.fetch_add(1, std::memory_order_relaxed);
    shared.frames.fetch_add(frameCount, std::memory_order_relaxed);
    raise(shared.largestCallbackFrames, frameCount);
    Mixer* const kMixer = shared.mixer.load(std::memory_order_acquire);
    if (kMixer == nullptr) {
        return;
    }
    const execution::MonotonicInstant kBegan = shared.clock.now();
    kMixer->render(std::span{static_cast<float*>(output), static_cast<std::size_t>(frameCount) * kChannels});
    const execution::MonotonicInstant kEnded = shared.clock.now();
    raise(shared.longestCallbackNanoseconds, (kEnded - kBegan).nanoseconds);
}

// A device that stops without its owner asking is lost: output suspends.
void notice(const ma_device_notification* notification) {
    auto& shared = *static_cast<Shared*>(notification->pDevice->pUserData);
    if (notification->type == ma_device_notification_type_stopped && !shared.stopping.load(std::memory_order_acquire)) {
        shared.mixer.store(nullptr, std::memory_order_release);
        shared.state.store(OutputState::Lost, std::memory_order_release);
    }
}

} // namespace

struct Output::State {
    ma_context context{};
    ma_device device{};
    bool contextOpen = false;
    bool deviceOpen = false;
    ma_backend backend = ma_backend_null;
    Shared shared;

    ~State() {
        if (deviceOpen) {
            ma_device_uninit(&device);
        }
        if (contextOpen) {
            ma_context_uninit(&context);
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

    // The platform's backends, never the null one: a machine without sound
    // has no device rather than a silent one it did not ask for.
    std::array<ma_backend, MA_BACKEND_COUNT> backends{};
    std::size_t count = 0;
    if (settings.backend == OutputBackend::Null) {
        backends[count++] = ma_backend_null;
    } else {
        std::array<ma_backend, MA_BACKEND_COUNT> enabled{};
        std::size_t enabledCount = 0;
        if (ma_get_enabled_backends(enabled.data(), enabled.size(), &enabledCount) == MA_SUCCESS) {
            for (std::size_t index = 0; index < enabledCount; ++index) {
                if (enabled[index] != ma_backend_null) {
                    backends[count++] = enabled[index];
                }
            }
        }
    }
    ma_context_config contextConfig = ma_context_config_init();
    if (count == 0 ||
        ma_context_init(backends.data(), static_cast<ma_uint32>(count), &contextConfig, &state->context) !=
            MA_SUCCESS) {
        return refuse(result::ErrorClass::Unavailable, AudioError::NoDevice, "no audio backend is available");
    }
    state->contextOpen = true;
    state->backend = state->context.backend;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kChannels;
    config.sampleRate = settings.rate;
    config.periodSizeInFrames = settings.periodFrames;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = &render;
    config.notificationCallback = &notice;
    config.pUserData = &state->shared;
    if (ma_device_init(&state->context, &config, &state->device) != MA_SUCCESS) {
        auto refused =
            refuse(result::ErrorClass::Unavailable, AudioError::NoDevice, "no output device could be opened");
        refused.error() = std::move(refused.error()).withContext("backend", ma_get_backend_name(state->backend));
        return refused;
    }
    state->deviceOpen = true;
    return std::unique_ptr<Output>{new Output{std::move(state)}};
}

std::uint32_t Output::rate() const noexcept {
    return state_->device.sampleRate;
}

std::string Output::backendName() const {
    return ma_get_backend_name(state_->backend);
}

result::Status Output::start(Mixer& mixer) {
    if (mixer.rate() != rate()) {
        return std::unexpected<result::Error>{
            refuse(result::ErrorClass::InvalidArgument, AudioError::BadSettings, "the mixer renders at another rate")
                .error()
                .withContext("mixer", std::to_string(mixer.rate()))
                .withContext("device", std::to_string(rate()))};
    }
    if (state_->shared.state.load(std::memory_order_acquire) == OutputState::Rendering) {
        return refuse(result::ErrorClass::FailedPrecondition, AudioError::BadSettings, "the output is rendering");
    }
    state_->shared.stopping.store(false, std::memory_order_release);
    state_->shared.mixer.store(&mixer, std::memory_order_release);
    state_->shared.state.store(OutputState::Rendering, std::memory_order_release);
    if (ma_device_start(&state_->device) != MA_SUCCESS) {
        state_->shared.mixer.store(nullptr, std::memory_order_release);
        state_->shared.state.store(OutputState::Lost, std::memory_order_release);
        return refuse(result::ErrorClass::Unavailable, AudioError::NoDevice, "the output device would not start");
    }
    return {};
}

void Output::stop() noexcept {
    state_->shared.stopping.store(true, std::memory_order_release);
    // Returns once the device's thread has left its last callback.
    ma_device_stop(&state_->device);
    state_->shared.mixer.store(nullptr, std::memory_order_release);
    if (state_->shared.state.load(std::memory_order_acquire) == OutputState::Rendering) {
        state_->shared.state.store(OutputState::Stopped, std::memory_order_release);
    }
}

OutputState Output::state() const noexcept {
    return state_->shared.state.load(std::memory_order_acquire);
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
