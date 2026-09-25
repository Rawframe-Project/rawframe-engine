// Output against ADR-0038: a device opens at its own rate or the one asked
// for, renders only a mixer made at that rate, from its own thread while the
// owner plays (ThreadSanitizer runs this in the full check), and stops for
// good when told; a machine without a device gets a typed refusal. The null
// backend stands in for a device, on a timer.

#include "rawframe/audio/errors.h"
#include "rawframe/audio/output.h"
#include "rawframe/test/test.h"

#include <chrono>
#include <thread>

using namespace rawframe;
using namespace rawframe::audio;

namespace {

Layout layout() {
    Layout made;
    made.buses.push_back(Bus{.id = 1, .name = "master", .role = Role::Master, .parent = 0});
    return made;
}

std::shared_ptr<const Clip> constant(float value, std::size_t frames, std::uint32_t rate) {
    auto clip = std::make_shared<Clip>();
    clip->channels = 1;
    clip->rate = rate;
    clip->samples.assign(frames, value);
    return clip;
}

/// Waits, within ten seconds however loaded the machine, until `ready`.
template <typename Ready> bool await(Ready ready) {
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= kDeadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

} // namespace

RAWFRAME_TEST(AnOutputOpensAtTheRateAskedOrItsOwn) {
    auto own = Output::open({.backend = OutputBackend::Null});
    RAWFRAME_EXPECT(own.has_value() && (*own)->rate() >= 8'000 && (*own)->state() == OutputState::Stopped);
    RAWFRAME_EXPECT(own.has_value() && (*own)->backendName() == "Null");
    auto asked = Output::open({.backend = OutputBackend::Null, .rate = 44'100});
    RAWFRAME_EXPECT(asked.has_value() && (*asked)->rate() == 44'100);

    auto noPeriod = Output::open({.backend = OutputBackend::Null, .periodFrames = 0});
    RAWFRAME_EXPECT(!noPeriod.has_value() && noPeriod.error().code() == code(AudioError::BadSettings));
    auto tooFast = Output::open({.backend = OutputBackend::Null, .rate = 400'000});
    RAWFRAME_EXPECT(!tooFast.has_value() && tooFast.error().code() == code(AudioError::BadSettings));
}

RAWFRAME_TEST(OnlyAMixerAtTheDevicesRateIsRendered) {
    auto output = *Output::open({.backend = OutputBackend::Null, .rate = 44'100});
    auto mixer = *Mixer::create(layout(), {.rate = 48'000});
    const result::Status kStarted = output->start(*mixer);
    RAWFRAME_EXPECT(!kStarted.has_value() && kStarted.error().code() == code(AudioError::BadSettings));
    RAWFRAME_EXPECT(output->state() == OutputState::Stopped);
}

RAWFRAME_TEST(TheDevicesThreadRendersTheMixerUntilStopped) {
    auto output = *Output::open({.backend = OutputBackend::Null, .periodFrames = 128});
    auto mixer = *Mixer::create(layout(), {.rate = output->rate()});
    RAWFRAME_EXPECT(output->start(*mixer).has_value());
    RAWFRAME_EXPECT(output->state() == OutputState::Rendering);
    const result::Status kAgain = output->start(*mixer);
    RAWFRAME_EXPECT(!kAgain.has_value());

    // A long quiet-ish tone; the master's meter shows the device rendering it.
    const auto kPlayback = mixer->play(constant(0.25F, output->rate() * 30, output->rate()), {.bus = 0});
    RAWFRAME_EXPECT(kPlayback.has_value());
    RAWFRAME_EXPECT(await([&] {
        return mixer->meter(0).peakLeft > 0.1F;
    }));
    RAWFRAME_EXPECT(await([&] {
        return output->statistics().callbacks >= 4;
    }));
    const OutputStatistics kRunning = output->statistics();
    RAWFRAME_EXPECT(kRunning.frames >= kRunning.callbacks && kRunning.largestCallbackFrames > 0 &&
                    kRunning.longestCallbackNanoseconds > 0);

    // The owner goes on playing while the device renders.
    for (int round = 0; round < 50; ++round) {
        mixer->collect();
        if (auto playback = mixer->play(constant(0.01F, 64, output->rate()), {.bus = 0}); playback.has_value()) {
            mixer->stop(*playback, 0.001F);
        }
        std::this_thread::yield();
    }

    output->stop();
    RAWFRAME_EXPECT(output->state() == OutputState::Stopped);
    const std::uint64_t kStopped = output->statistics().callbacks;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    RAWFRAME_EXPECT(output->statistics().callbacks == kStopped);

    // It starts again, as a client coming back to the foreground would.
    RAWFRAME_EXPECT(output->start(*mixer).has_value());
    RAWFRAME_EXPECT(await([&] {
        return output->statistics().callbacks > kStopped;
    }));
    output->stop();
}

RAWFRAME_TEST(AMachineWithoutADeviceIsRefusedByType) {
    // Whatever this machine has: a device, or a refusal that says there is
    // none, never a silent null device standing in for one.
    auto output = Output::open({});
    if (output.has_value()) {
        RAWFRAME_EXPECT((*output)->backendName() != "Null");
    } else {
        RAWFRAME_EXPECT(output.error().errorClass() == result::ErrorClass::Unavailable &&
                        output.error().code() == code(AudioError::NoDevice));
    }
}
