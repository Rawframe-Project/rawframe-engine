#pragma once

// A World heard (ADR-0038): entities with `rawframe.sound` emitters play
// their declared sounds from where their poses put them, heard by the one
// active listener. Client only; a server carries the same components as
// plain values and links none of this.

#include "rawframe/audio/layout.h"
#include "rawframe/audio/sounds.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/world.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::world_audio {

/// `rawframe.sound.Emitter` as C++ reads it; checked against the program's
/// layout when a game's audio loads.
struct Emitter {
    std::uint64_t sound = 0;
    std::uint32_t cue = 0;
    bool playing = false;
    std::uint8_t despawn = 0;
};

/// `rawframe.sound.Listener`.
struct Listener {
    bool active = false;
};

/// The despawn policies' values in `Emitter::despawn`.
inline constexpr std::uint8_t kStop = 0;
inline constexpr std::uint8_t kFadeOut = 1;
inline constexpr std::uint8_t kDetachToCompletion = 2;

/// The most one-shots one emitter starts in one update, however far its cue
/// moved.
inline constexpr std::uint32_t kMaximumCuesPerUpdate = 4;

struct WorldAudioSettings {
    /// The game's components of the two types; a game whose clients bind
    /// their listener to their player may have no listener component.
    schema::ComponentTypeId emitter;
    std::optional<schema::ComponentTypeId> listener;
    /// Each declared sound's identity and its index in the Sounds.
    std::vector<std::pair<std::uint64_t, std::size_t>> sounds;
};

struct WorldAudioStatistics {
    /// Updates with more than one active listener, which hear nothing.
    std::uint64_t listenerConflicts = 0;
    /// Emitters naming a sound the game does not declare, each update.
    std::uint64_t unknownSounds = 0;
    std::uint64_t cues = 0;
    /// Cues of looping sounds, which only `playing` plays.
    std::uint64_t loopingCues = 0;
    /// Plays the Sounds refused (a full concurrency set, no voice).
    std::uint64_t refused = 0;
};

class WorldAudio {
public:
    [[nodiscard]] static result::Result<std::unique_ptr<WorldAudio>>
    create(const schema::SchemaRegistry& registry, audio::Sounds& sounds, WorldAudioSettings settings);

    WorldAudio(const WorldAudio&) = delete;
    WorldAudio& operator=(const WorldAudio&) = delete;
    ~WorldAudio();

    /// Binds the listener to an entity explicitly (SPEC-0036's typed
    /// binding): a networked client hears from its own player, which only it
    /// knows. While bound, `Listener` components are not read; an entity
    /// that is gone, or has no pose, hears from nowhere.
    void bindListener(std::optional<world::EntityHandle> entity);

    /// Once a frame, on the owner's thread: the listener, every emitter's
    /// continuous sound and new cues, what despawned, then the Sounds.
    void update(world::World& world, float seconds);

    [[nodiscard]] const WorldAudioStatistics& statistics() const noexcept;

private:
    struct State;
    explicit WorldAudio(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// A game's sound, loaded: its mixer layout, its sounds with their
/// identities, and its emitter and listener components.
struct GameAudio {
    audio::Layout layout;
    std::vector<std::pair<std::uint64_t, audio::LoadedSound>> sounds;
    schema::ComponentTypeId emitter;
    std::optional<schema::ComponentTypeId> listener;
};

/// Reads the game description at `game` and what its `mixer` and `sound`
/// lines name, and finds its components of `rawframe.sound`'s types (an
/// emitter, and a listener if it has one),
/// whose layouts in `program` must be what this module reads. Refuses
/// (`NotFound`) a game with no mixer line.
[[nodiscard]] result::Result<GameAudio> loadGameAudio(const std::string& game, const kest::Program& program);

} // namespace rawframe::world_audio
