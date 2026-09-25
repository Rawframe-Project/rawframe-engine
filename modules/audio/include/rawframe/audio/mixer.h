#pragma once

// The mixer (ADR-0038): playing sounds summed through the layout's buses,
// their effects, faders, and sends, into interleaved stereo. The owner
// thread plays, stops, and changes things through a bounded command queue;
// the mix thread renders without locks, allocation, or blocking, and hands
// back what finished through a second queue, so every release happens on
// the owner's thread.

#include "rawframe/audio/layout.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rawframe::audio {

/// Decoded sound in memory: interleaved float samples of one or two
/// channels at a rate. Immutable once made; shared by every playback of it.
struct Clip {
    std::vector<float> samples;
    std::uint32_t channels = 1;
    std::uint32_t rate = 48'000;

    [[nodiscard]] std::size_t frames() const noexcept {
        return channels == 0 ? 0 : samples.size() / channels;
    }
};

/// A playback, by its voice and that voice's use.
struct Playback {
    std::uint32_t voice = 0;
    std::uint32_t generation = 0;
    friend constexpr bool operator==(const Playback&, const Playback&) noexcept = default;
};

struct PlayParameters {
    /// The bus it plays into, by index in the layout.
    std::size_t bus = 0;
    float volume = 0;
    /// A ratio: two plays an octave up and twice as fast.
    float pitch = 1;
    /// From hard left (minus one) to hard right (one).
    float pan = 0;
    bool loop = false;
};

enum class PlaybackState : std::uint8_t {
    Playing,
    Stopping,
    Finished
};

/// A bus's level over the last rendered block, per channel, in linear
/// amplitude.
struct BusMeter {
    float peakLeft = 0;
    float peakRight = 0;
    float rmsLeft = 0;
    float rmsRight = 0;
};

/// The mixer's profile values (ADR-0038: never constants in code).
struct MixerSettings {
    std::uint32_t rate = 48'000;
    /// The most frames one render step takes; a longer request is rendered
    /// in steps.
    std::uint32_t blockFrames = 256;
    std::uint32_t voices = 64;
    std::uint32_t commandQueue = 1024;
    /// A stop without a fade still fades over this long, so it never clicks.
    float shortestFade = 0.005F;
};

struct MixerStatistics {
    /// Plays refused because every voice was in use.
    std::uint64_t voicesExhausted = 0;
    /// Commands refused because the queue to the mix thread was full.
    std::uint64_t queueFull = 0;
};

class Mixer {
public:
    [[nodiscard]] static result::Result<std::unique_ptr<Mixer>> create(const Layout& layout,
                                                                       const MixerSettings& settings);

    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    ~Mixer();

    // The owner's thread.

    /// Starts a clip. Refuses (`ResourceExhausted`) when every voice is in
    /// use or the command queue is full, and a bus the layout lacks.
    [[nodiscard]] result::Result<Playback> play(std::shared_ptr<const Clip> clip, const PlayParameters& parameters);
    /// Fades out over `fade` seconds (at least the shortest fade), then
    /// finishes.
    void stop(Playback playback, float fade = 0);
    void setVolume(Playback playback, float decibels);
    void setPitch(Playback playback, float pitch);
    void setBusVolume(std::size_t bus, float decibels);
    void setBusMuted(std::size_t bus, bool muted);
    /// Takes what the mix thread handed back: finished playbacks are
    /// released and their voices reusable.
    void collect();
    [[nodiscard]] PlaybackState state(Playback playback) const noexcept;
    [[nodiscard]] BusMeter meter(std::size_t bus) const noexcept;
    [[nodiscard]] const MixerStatistics& statistics() const noexcept;

    // The mix thread.

    /// Renders `output.size() / 2` frames of interleaved stereo. Takes no
    /// lock, allocates nothing, and never blocks.
    void render(std::span<float> output) noexcept;

private:
    struct State;
    explicit Mixer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::audio
