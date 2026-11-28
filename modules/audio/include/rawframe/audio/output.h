#pragma once

// Sound reaching a device (the device I/O provider): an output opens
// a device at its own rate, and its real-time thread renders a mixer made at
// that rate into every buffer the device asks for. It plays on the system's
// default device, following it as it changes, or on a device a player chose
// by its key, falling back to the default while that device is away. The
// provider stays behind this header; nothing of it appears here. Client
// only.

#include "rawframe/audio/mixer.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::audio {

enum class OutputBackend : std::uint8_t {
    /// The platform's devices, most preferred first.
    Default,
    /// A device that takes buffers on a timer and plays nothing: machines
    /// without sound, and tests.
    Null,
};

/// The output's profile values (never constants in code).
struct OutputSettings {
    OutputBackend backend = OutputBackend::Default;
    /// Frames a second; nought for the device's own, so the mixer renders at
    /// it and nothing resamples between them.
    std::uint32_t rate = 0;
    /// The buffer the device asks for at a time, in frames.
    std::uint32_t periodFrames = 256;
    /// The key of the device a player chose (`OutputDevice::key`), or empty
    /// to follow the system's default. A key no device has yet plays on the
    /// default until that device comes.
    std::string device;
};

/// The longest device key an output takes, in bytes.
inline constexpr std::size_t kMaxDeviceKeyBytes = 1024;

/// An output device, as the platform lists it.
struct OutputDevice {
    /// The same for the same device after it is unplugged and plugged
    /// again, and across restarts where the platform allows: what a player's
    /// choice is stored by. A device's id lasts only while it is present.
    std::string key;
    /// For a player to choose by.
    std::string name;
    /// Whether it is the system's default output.
    bool isDefault = false;
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
    /// Times the output went to another device: the default changed, the
    /// chosen device went or came back, or a player chose another.
    std::uint64_t moves = 0;
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

    /// The output devices there are now, in the platform's stable order.
    [[nodiscard]] result::Result<std::vector<OutputDevice>> devices() const;
    /// Plays on the device with `key` from now on, or on the default for an
    /// empty key; a key no device has yet plays on the default until that
    /// device comes. Refuses a key longer than kMaxDeviceKeyBytes.
    [[nodiscard]] result::Status choose(std::string_view key);
    /// The chosen key, empty when the output follows the default.
    [[nodiscard]] std::string chosen() const;
    /// The key of the device it plays on now; empty while it has none.
    [[nodiscard]] std::string device() const;
    /// Takes in what changed in the devices since the last call and moves
    /// to the device it should be on: to the new default when it follows
    /// the default, to the default when the chosen device goes, and back to
    /// the chosen one when its key comes back. The owner calls it regularly,
    /// as once a frame, from its own thread. True when it moved.
    bool follow();

    /// The null backend's devices are scripted, as a platform's are plugged
    /// and unplugged, so that following them is tested without hardware.
    /// Each refuses (`FailedPrecondition`) on any other backend; a device
    /// becomes the default when its direction had none. The changes are
    /// taken in at the next `follow`.
    [[nodiscard]] result::Status plug(std::string_view key, std::string_view name);
    [[nodiscard]] result::Status unplug(std::string_view key);
    [[nodiscard]] result::Status makeDefault(std::string_view key);

private:
    struct State;
    explicit Output(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::audio
