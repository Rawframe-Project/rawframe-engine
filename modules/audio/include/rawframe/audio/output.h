#pragma once

// Sound reaching a device (ADR-0038's device I/O provider): an output opens
// a device at its own rate, and its real-time thread renders a mixer made at
// that rate into every buffer the device asks for. The provider stays behind
// this header; nothing of it appears here. Client only.

#include "rawframe/audio/mixer.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>
#include <string>

namespace rawframe::audio {

enum class OutputBackend : std::uint8_t {
    /// The platform's devices, most preferred first.
    Default,
    /// A device that takes buffers on a timer and plays nothing: machines
    /// without sound, and tests.
    Null,
};

/// The output's profile values (ADR-0038: never constants in code).
struct OutputSettings {
    OutputBackend backend = OutputBackend::Default;
    /// Frames a second; nought for the device's own, so the mixer renders at
    /// it and nothing resamples between them.
    std::uint32_t rate = 0;
    /// The buffer the device asks for at a time, in frames.
    std::uint32_t periodFrames = 256;
};

enum class OutputState : std::uint8_t {
    /// Opened, not rendering.
    Stopped,
    Rendering,
    /// The device stopped by itself (unplugged, taken away): output is
    /// suspended and the mixer is not rendered. Sounds keep their state.
    Lost,
};

struct OutputStatistics {
    /// Buffers the device asked for, and their frames.
    std::uint64_t callbacks = 0;
    std::uint64_t frames = 0;
    std::uint32_t largestCallbackFrames = 0;
    /// The longest one render into a buffer took.
    std::int64_t longestCallbackNanoseconds = 0;
};

class Output {
public:
    /// Opens a device for interleaved stereo. Refuses (`Unavailable`,
    /// `NoDevice`) when there is none: a client runs on without sound.
    [[nodiscard]] static result::Result<std::unique_ptr<Output>> open(const OutputSettings& settings);

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    /// Stops first.
    ~Output();

    /// The rate the device opened at; make the mixer at it.
    [[nodiscard]] std::uint32_t rate() const noexcept;
    /// What opened it, for diagnostics.
    [[nodiscard]] std::string backendName() const;

    /// Renders `mixer` from the device's thread until `stop`. Refuses a
    /// mixer at another rate, and a start while rendering. The mixer outlives
    /// the rendering; its owner's thread stays the one calling it.
    [[nodiscard]] result::Status start(Mixer& mixer);
    /// Returns once the device's thread no longer renders the mixer.
    void stop() noexcept;

    [[nodiscard]] OutputState state() const noexcept;
    /// Read from any thread; each value on its own.
    [[nodiscard]] OutputStatistics statistics() const noexcept;

private:
    struct State;
    explicit Output(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::audio
