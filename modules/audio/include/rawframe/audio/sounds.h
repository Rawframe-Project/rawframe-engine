#pragma once

// Declared sounds playing (ADR-0038's asset and instance split): each play
// of a declaration is an instance that picks its variant, draws its volume
// and pitch, counts in its concurrency set, fades with its distance from
// the listener, and goes virtual rather than silent when it has no voice.
// The owner's thread only; the mixer does the rest.

#include "rawframe/audio/decode.h"
#include "rawframe/audio/mixer.h"
#include "rawframe/audio/sound.h"
#include "rawframe/audio/stream.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::audio {

/// A point in the World, in meters.
struct Position {
    float x = 0;
    float y = 0;
    float z = 0;
};

/// Who hears: where, and which way is their right.
struct Listener {
    Position position;
    Position right{.x = 1, .y = 0, .z = 0};
};

/// One play of a declared sound, by its slot and that slot's use.
struct Instance {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
    friend constexpr bool operator==(const Instance&, const Instance&) noexcept = default;
};

enum class InstanceState : std::uint8_t {
    Playing,
    /// Logically playing without a voice: out of range, or none free.
    Virtual,
    Stopping,
    Finished,
};

/// A declaration with its variants, in their order: decoded clips for a
/// preloaded sound, cooked Opus for a streamed one.
struct LoadedSound {
    SoundDeclaration declaration;
    std::vector<std::shared_ptr<const Clip>> clips;
    std::vector<std::shared_ptr<const std::vector<std::byte>>> cooked;
};

/// Reads the declaration at `path` against `layout` and its variants,
/// beside it: decoded for a preloaded sound, checked and kept cooked for a
/// streamed one.
[[nodiscard]] result::Result<LoadedSound>
loadSound(const std::string& path, const Layout& layout, const DecodeLimits& limits = {});

struct SoundsSettings {
    /// Where variant choices and drawn volumes and pitches come from.
    std::uint64_t seed = 0;
    std::size_t maximumInstances = 256;
    /// What keeps streamed sounds decoded ahead; a Sounds without one
    /// refuses them.
    Streamer* streamer = nullptr;
    /// Each playing stream's ring, in frames.
    std::size_t streamBufferFrames = 48'000;
};

struct SoundsStatistics {
    /// Plays a full concurrency set turned away.
    std::uint64_t refusedByConcurrency = 0;
    /// Instances a full concurrency set stopped for a newer one.
    std::uint64_t evicted = 0;
    std::uint64_t virtualized = 0;
    std::uint64_t revived = 0;
    /// Instances stopped for being out of range, without virtualization.
    std::uint64_t culled = 0;
};

class Sounds {
public:
    [[nodiscard]] static result::Result<std::unique_ptr<Sounds>>
    create(Mixer& mixer, const Layout& layout, const SoundsSettings& settings);

    Sounds(const Sounds&) = delete;
    Sounds& operator=(const Sounds&) = delete;
    ~Sounds();

    /// Adds a sound; its index plays it. Refuses clips or cooked variants
    /// that are not one a variant or not what its loading asks, loop points
    /// past a clip's end, and a streamed sound without a streamer.
    [[nodiscard]] result::Result<std::size_t> add(LoadedSound sound);

    /// Plays sound `sound`, at `at` if it is spatial. Refuses a play its
    /// full concurrency set turns away, and one without a voice that may
    /// not go virtual.
    [[nodiscard]] result::Result<Instance> play(std::size_t sound, std::optional<Position> at = std::nullopt);
    void stop(Instance instance, float fade = 0);
    void move(Instance instance, Position at);
    /// None: no one hears spatial sounds, which follow their declaration.
    void setListener(std::optional<Listener> listener);
    /// Once a frame: takes what the mixer finished, keeps virtual positions,
    /// sets each spatial instance's gain and pan, goes virtual out of range,
    /// comes back in, and has streams decoded ahead.
    void update(float seconds);

    [[nodiscard]] InstanceState state(Instance instance) const noexcept;
    /// Sound `sound`'s declaration, or null for one not added.
    [[nodiscard]] const SoundDeclaration* declaration(std::size_t sound) const noexcept;
    [[nodiscard]] const SoundsStatistics& statistics() const noexcept;

private:
    struct State;
    explicit Sounds(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::audio
