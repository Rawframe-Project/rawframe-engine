#include "rawframe/audio/output.h"

#include "rawframe/audio/errors.h"
#include "rawframe/execution/time.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <maul-audio/context.h>
#include <maul-audio/device.h>
#include <maul-audio/notification.h>
#include <maul-audio/offline.h>
#include <maul-audio/stream.h>
#include <mutex>
#include <optional>
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
                                              .withContext("outcome", maudGetResultName(outcome))};
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

bool same(maudDeviceId left, maudDeviceId right) noexcept {
    return left.index1 == right.index1 && left.generation == right.generation;
}

bool same(maudStreamId left, maudStreamId right) noexcept {
    return left.index1 == right.index1 && left.generation == right.generation;
}

/// A device's name or key: empty when the device is gone.
template <typename Read> std::string text(const maudContext* context, maudDeviceId device, Read read) {
    std::string bytes(256, '\0');
    std::size_t length = 0;
    maudResult outcome = read(context, device, bytes.data(), bytes.size(), &length);
    if (outcome == maud_errorCapacity) {
        bytes.resize(length);
        outcome = read(context, device, bytes.data(), bytes.size(), &length);
    }
    if (outcome != maud_success) {
        return {};
    }
    bytes.resize(length);
    return bytes;
}

std::vector<maudDeviceId> outputs(const maudContext* context) {
    std::uint32_t count = 0;
    if (maudContext_GetDevices(context, maud_directionOutput, nullptr, 0, &count) != maud_success || count == 0) {
        return {};
    }
    std::vector<maudDeviceId> ids(count);
    if (maudContext_GetDevices(context, maud_directionOutput, ids.data(), count, &count) != maud_success) {
        return {};
    }
    ids.resize(std::min<std::size_t>(ids.size(), count));
    return ids;
}

/// The present device with `key`, or none.
std::optional<maudDeviceId> withKey(const maudContext* context, std::string_view key) {
    if (key.empty()) {
        return std::nullopt;
    }
    for (const maudDeviceId kDevice : outputs(context)) {
        if (text(context, kDevice, maudDevice_GetKey) == key) {
            return kDevice;
        }
    }
    return std::nullopt;
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
    std::atomic<std::uint64_t> moves{0};
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
    std::uint32_t periodAsked = 0;
    bool rendering = false;
    bool lost = false;
    /// The key a player chose, empty to follow the default.
    std::string chosen;
    /// The device the stream was made on: the null id follows the default.
    maudDeviceId openedOn{};
    Shared shared;
    /// The context is used by one thread at a time: the owner's, and the
    /// null device's thread while it renders.
    std::mutex lock;
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

    [[nodiscard]] bool offline() const noexcept {
        return backend == maud_backendOffline;
    }

    void startPacing() {
        pacing.store(true, std::memory_order_release);
        pacer = std::thread([this] {
            std::vector<float> buffer(static_cast<std::size_t>(period) * kChannels);
            const auto kPeriod = std::chrono::nanoseconds{std::int64_t{1'000'000'000} * period / rate};
            auto next = std::chrono::steady_clock::now();
            while (pacing.load(std::memory_order_acquire)) {
                {
                    const std::lock_guard kHeld{lock};
                    static_cast<void>(maudStream_Render(context, stream, buffer.data(), period));
                }
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

    [[nodiscard]] maudStreamDef streamDef(maudDeviceId device) noexcept {
        maudStreamDef def = maudDefaultStreamDef();
        def.direction = maud_directionOutput;
        // The offline backend is pulled by the null device's thread; a
        // device's calls back from its own.
        def.mode = offline() ? maud_modePull : maud_modeCallback;
        def.layout = maud_layoutStereo;
        def.periodFrames = periodAsked;
        def.device = device;
        def.callback = &render;
        def.user = &shared;
        return def;
    }

    /// Makes the stream again on `device` (the null id for the default), at
    /// the rate the mixer renders at, started if it was. The lock is held.
    bool reopen(maudDeviceId device) noexcept {
        if (stream.index1 != 0) {
            // Returns once the device's thread has left its last callback.
            static_cast<void>(maudDestroyStream(context, stream));
            stream = {};
        }
        // The platform converts where it can; the null device runs at any
        // rate without conversion, which a device may also do.
        maudStreamDef def = streamDef(device);
        def.sampleRate = rate;
        constexpr std::array<maudRatePolicy, 2> kPolicies{maud_ratePlatformConverted, maud_rateRequired};
        maudResult made = maud_errorUnsupported;
        for (const maudRatePolicy kPolicy : std::span{kPolicies}.subspan(offline() ? 1 : 0)) {
            def.ratePolicy = kPolicy;
            made = maudCreateStream(context, &def, &stream);
            if (made == maud_success) {
                break;
            }
        }
        shared.moves.fetch_add(1, std::memory_order_relaxed);
        if (made != maud_success) {
            stream = {};
            return true;
        }
        openedOn = device;
        lost = rendering && maudStream_Start(context, stream) != maud_success;
        return true;
    }

    /// Puts the stream on the device it should be on; true when it moved.
    /// The lock is held.
    bool place() noexcept {
        const maudDeviceId kWanted = withKey(context, chosen).value_or(maudDeviceId{});
        bool stays = stream.index1 != 0 && same(kWanted, openedOn);
        if (stays) {
            maudStreamStatus status{};
            maudStreamFormat format{};
            stays = maudStream_GetStatus(context, stream, &status) == maud_success &&
                    status.suspension != maud_suspendDeviceLost &&
                    maudStream_GetFormat(context, stream, &format) == maud_success && format.sampleRate == rate;
        }
        return !stays && reopen(kWanted);
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
    if (settings.device.size() > kMaxDeviceKeyBytes) {
        return refuse(result::ErrorClass::InvalidArgument, AudioError::BadSettings, "the device key is too long");
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
    state->backend = maudContext_GetBackend(state->context);
    state->chosen = settings.device;
    state->periodAsked = settings.periodFrames;
    // The chosen device when it is here, or else the default, followed.
    const maudDeviceId kOn = withKey(state->context, state->chosen).value_or(maudDeviceId{});
    maudDeviceId fallback{};
    if (const maudResult kFound =
            maudContext_GetDefaultDevice(state->context, maud_directionOutput, maud_roleGeneral, &fallback);
        kOn.index1 == 0 && (kFound != maud_success || fallback.index1 == 0)) {
        return std::unexpected<result::Error>{noDevice("no output device could be opened", kFound)
                                                  .error()
                                                  .withContext("backend", std::string{nameOf(state->backend)})};
    }

    maudStreamDef streamDef = state->streamDef(kOn);
    streamDef.ratePolicy = settings.rate != 0 && !kNull ? maud_ratePlatformConverted : maud_rateNative;
    streamDef.sampleRate = settings.rate != 0 && !kNull ? settings.rate : 0;
    if (const maudResult kMade = maudCreateStream(state->context, &streamDef, &state->stream); kMade != maud_success) {
        return std::unexpected<result::Error>{noDevice("no output device could be opened", kMade)
                                                  .error()
                                                  .withContext("backend", std::string{nameOf(state->backend)})};
    }
    state->openedOn = kOn;
    maudStreamFormat format{};
    if (const maudResult kRead = maudStream_GetFormat(state->context, state->stream, &format); kRead != maud_success) {
        return noDevice("the output device's format could not be read", kRead);
    }
    state->rate = format.sampleRate;
    state->period = format.periodFrames != 0 ? format.periodFrames : settings.periodFrames;
    // What opening it reported is already taken in.
    maudNotification drained{};
    while (maudContext_NextNotification(state->context, &drained) == maud_success) {
    }
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
    {
        const std::lock_guard kHeld{state_->lock};
        if (const maudResult kStarted = maudStream_Start(state_->context, state_->stream); kStarted != maud_success) {
            state_->shared.mixer.store(nullptr, std::memory_order_release);
            state_->lost = true;
            return noDevice("the output device would not start", kStarted);
        }
    }
    state_->rendering = true;
    state_->lost = false;
    if (state_->offline()) {
        state_->startPacing();
    }
    return {};
}

void Output::stop() noexcept {
    state_->stopPacing();
    if (state_->rendering) {
        // Returns once the device's thread has left its last callback.
        const std::lock_guard kHeld{state_->lock};
        static_cast<void>(maudStream_Stop(state_->context, state_->stream));
        state_->rendering = false;
    }
    state_->shared.mixer.store(nullptr, std::memory_order_release);
}

OutputState Output::state() const noexcept {
    if (!state_->rendering) {
        return state_->lost ? OutputState::Lost : OutputState::Stopped;
    }
    // A device that went away, or a default that left no device, suspends
    // the stream: output is lost, and the mixer is not rendered. So is a
    // stream that could not be made again on the device it moved to.
    const std::lock_guard kHeld{state_->lock};
    maudStreamStatus status{};
    if (state_->stream.index1 == 0 || state_->lost ||
        (maudStream_GetStatus(state_->context, state_->stream, &status) == maud_success &&
         (status.suspension == maud_suspendDeviceLost || status.suspension == maud_suspendNoDevice))) {
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
        .moves = state_->shared.moves.load(std::memory_order_relaxed),
    };
}

result::Result<std::vector<OutputDevice>> Output::devices() const {
    const std::lock_guard kHeld{state_->lock};
    std::vector<OutputDevice> listed;
    for (const maudDeviceId kDevice : outputs(state_->context)) {
        maudDeviceInfo info{};
        if (maudDevice_GetInfo(state_->context, kDevice, &info) != maud_success) {
            continue;
        }
        listed.push_back(OutputDevice{.key = text(state_->context, kDevice, maudDevice_GetKey),
                                      .name = text(state_->context, kDevice, maudDevice_GetName),
                                      .isDefault = info.defaultGeneral});
    }
    return listed;
}

result::Status Output::choose(std::string_view key) {
    if (key.size() > kMaxDeviceKeyBytes) {
        return refuse(result::ErrorClass::InvalidArgument, AudioError::BadSettings, "the device key is too long");
    }
    const std::lock_guard kHeld{state_->lock};
    state_->chosen = std::string{key};
    static_cast<void>(state_->place());
    return {};
}

std::string Output::chosen() const {
    const std::lock_guard kHeld{state_->lock};
    return state_->chosen;
}

std::string Output::device() const {
    const std::lock_guard kHeld{state_->lock};
    maudStreamStatus status{};
    if (state_->stream.index1 == 0 || maudStream_GetStatus(state_->context, state_->stream, &status) != maud_success ||
        status.device.index1 == 0) {
        return {};
    }
    return text(state_->context, status.device, maudDevice_GetKey);
}

bool Output::follow() {
    const std::lock_guard kHeld{state_->lock};
    bool moved = false;
    bool look = false;
    maudNotification notification{};
    while (maudContext_NextNotification(state_->context, &notification) == maud_success) {
        const bool kOurs = same(notification.stream, state_->stream);
        switch (notification.kind) {
        case maud_notifyDeviceAdded:
        case maud_notifyDeviceRemoved:
        case maud_notifyDefaultChanged:
            look = look || notification.direction == maud_directionOutput;
            break;
        case maud_notifyStreamMoved:
            // The library moved a stream that follows the default.
            if (kOurs) {
                state_->shared.moves.fetch_add(1, std::memory_order_relaxed);
                moved = true;
            }
            break;
        case maud_notifyStreamSuspended:
        case maud_notifyStreamResumed:
        case maud_notifyStreamFormatChanged:
            look = look || kOurs;
            break;
        case maud_notifyOverflow:
            look = true;
            break;
        default:
            break;
        }
    }
    if (look) {
        moved = state_->place() || moved;
    }
    return moved;
}

namespace {

std::unexpected<result::Error> scriptedOnly() {
    return refuse(result::ErrorClass::FailedPrecondition,
                  AudioError::BadSettings,
                  "only the null backend's devices are scripted");
}

} // namespace

result::Status Output::plug(std::string_view key, std::string_view name) {
    if (!state_->offline()) {
        return scriptedOnly();
    }
    const std::lock_guard kHeld{state_->lock};
    maudOfflineDeviceDef def = maudDefaultOfflineDeviceDef();
    def.sampleRate = state_->rate;
    def.name = name.data();
    def.nameLength = name.size();
    def.key = key.data();
    def.keyLength = key.size();
    maudDeviceId device{};
    if (const maudResult kMade = maudCreateOfflineDevice(state_->context, &def, &device); kMade != maud_success) {
        return noDevice("the null device could not be plugged", kMade);
    }
    return {};
}

result::Status Output::unplug(std::string_view key) {
    if (!state_->offline()) {
        return scriptedOnly();
    }
    const std::lock_guard kHeld{state_->lock};
    const std::optional<maudDeviceId> kDevice = withKey(state_->context, key);
    if (!kDevice.has_value()) {
        return noDevice("no null device has that key", maud_errorStale);
    }
    if (const maudResult kGone = maudDestroyOfflineDevice(state_->context, *kDevice); kGone != maud_success) {
        return noDevice("the null device could not be unplugged", kGone);
    }
    return {};
}

result::Status Output::makeDefault(std::string_view key) {
    if (!state_->offline()) {
        return scriptedOnly();
    }
    const std::lock_guard kHeld{state_->lock};
    const std::optional<maudDeviceId> kDevice = withKey(state_->context, key);
    if (!kDevice.has_value()) {
        return noDevice("no null device has that key", maud_errorStale);
    }
    if (const maudResult kSet = maudDevice_SetOfflineDefault(state_->context, *kDevice, maud_roleGeneral);
        kSet != maud_success) {
        return noDevice("the null device could not be made the default", kSet);
    }
    return {};
}

} // namespace rawframe::audio
