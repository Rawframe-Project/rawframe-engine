#include "rawframe/world_audio/world_audio.h"

#include "rawframe/physics2d/components.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_audio/errors.h"
#include "rawframe/world_kest/game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>

namespace rawframe::world_audio {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldAudioError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kWorldAudioDomain, code(error), why).error()};
}

/// An emitter as the owner follows it between updates.
struct Tracked {
    std::optional<audio::Instance> continuous;
    std::uint32_t lastCue = 0;
    std::uint8_t despawn = kStop;
    /// The sound it plays, by index in the Sounds.
    std::size_t sound = 0;
    bool seen = false;
};

} // namespace

struct WorldAudio::State {
    audio::Sounds* sounds = nullptr;
    WorldAudioSettings settings;
    WorldAudioStatistics statistics;
    std::optional<world::ColumnQuery> emitters;
    std::optional<world::ColumnQuery> listeners;
    std::optional<schema::ComponentRuntimeId> pose2d;
    std::optional<schema::ComponentRuntimeId> pose3d;
    std::map<world::EntityHandle, Tracked> followed;
    std::optional<world::EntityHandle> bound;

    [[nodiscard]] std::optional<std::size_t> soundOf(std::uint64_t id) const noexcept {
        const auto kFound = std::ranges::find(settings.sounds, id, &std::pair<std::uint64_t, std::size_t>::first);
        return kFound == settings.sounds.end() ? std::nullopt : std::optional{kFound->second};
    }

    /// Where an entity is and which way is its right, from its pose.
    [[nodiscard]] std::optional<audio::Listener> placeOf(const world::World& world, world::EntityHandle entity) const {
        if (pose3d) {
            if (const auto* pose = static_cast<const physics3d::Pose3D*>(world.getErased(entity, *pose3d))) {
                const bool kTurned = pose->qx != 0 || pose->qy != 0 || pose->qz != 0 || pose->qw != 0;
                const float kX = pose->qx;
                const float kY = pose->qy;
                const float kZ = pose->qz;
                const float kW = kTurned ? pose->qw : 1.0F;
                return audio::Listener{.position = {.x = static_cast<float>(pose->x),
                                                    .y = static_cast<float>(pose->y),
                                                    .z = static_cast<float>(pose->z)},
                                       .right = {.x = 1 - (2 * ((kY * kY) + (kZ * kZ))),
                                                 .y = 2 * ((kX * kY) + (kW * kZ)),
                                                 .z = 2 * ((kX * kZ) - (kW * kY))}};
            }
        }
        if (pose2d) {
            if (const auto* pose = static_cast<const physics2d::Pose2D*>(world.getErased(entity, *pose2d))) {
                const bool kTurned = pose->c != 0 || pose->s != 0;
                return audio::Listener{
                    .position = {.x = static_cast<float>(pose->x), .y = static_cast<float>(pose->y), .z = 0},
                    .right = {.x = kTurned ? pose->c : 1.0F, .y = kTurned ? pose->s : 0.0F, .z = 0}};
            }
        }
        return std::nullopt;
    }

    void play(Tracked& follow, std::size_t sound, std::optional<audio::Position> at, bool continuous) {
        auto instance = sounds->play(sound, at);
        if (!instance.has_value()) {
            ++statistics.refused;
            return;
        }
        if (continuous) {
            follow.continuous = *instance;
        }
    }

    void update(world::World& world, float seconds) {
        // The bound listener, or the one active listener, or none.
        std::optional<audio::Listener> heard;
        std::size_t active = 0;
        if (bound) {
            heard = world.alive(*bound) ? placeOf(world, *bound) : std::nullopt;
        } else if (listeners) {
            listeners->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
                const auto* values = reinterpret_cast<const Listener*>(chunk.columns[0]);
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    if (values[row].active) {
                        ++active;
                        heard = placeOf(world, chunk.entities[row]).value_or(audio::Listener{});
                    }
                }
            });
        }
        if (active > 1) {
            ++statistics.listenerConflicts;
            heard.reset();
        }
        sounds->setListener(heard);

        for (auto& [entity, follow] : followed) {
            follow.seen = false;
        }
        std::vector<std::pair<world::EntityHandle, Emitter>> present;
        emitters->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                Emitter value;
                std::memcpy(&value, chunk.columns[0] + (row * sizeof(Emitter)), sizeof value);
                present.emplace_back(chunk.entities[row], value);
            }
        });
        for (const auto& [kEntity, kEmitter] : present) {
            const auto [kFound, kNew] = followed.try_emplace(kEntity);
            Tracked& follow = kFound->second;
            follow.seen = true;
            follow.despawn = kEmitter.despawn;
            // What the cue counted before it was first seen is history, and
            // it counts whether or not the emitter names a sound yet.
            if (kNew) {
                follow.lastCue = kEmitter.cue;
            }
            const std::uint32_t kCues = kEmitter.cue - follow.lastCue;
            follow.lastCue = kEmitter.cue;
            // Sound nought is none: an emitter that is quiet for now.
            if (kEmitter.sound == 0) {
                continue;
            }
            const auto kSound = soundOf(kEmitter.sound);
            if (!kSound) {
                ++statistics.unknownSounds;
                continue;
            }
            follow.sound = *kSound;
            // An emitter without a pose sounds where the listener is.
            const auto kPlace = placeOf(world, kEntity);
            const std::optional<audio::Position> kAt =
                kPlace ? std::optional{kPlace->position} : (heard ? std::optional{heard->position} : std::nullopt);
            if (kCues != 0) {
                if (sounds->declaration(*kSound)->loop) {
                    statistics.loopingCues += kCues;
                } else {
                    for (std::uint32_t cue = 0; cue < std::min(kCues, kMaximumCuesPerUpdate); ++cue) {
                        ++statistics.cues;
                        play(follow, *kSound, kAt, false);
                    }
                }
            }
            if (follow.continuous && sounds->state(*follow.continuous) == audio::InstanceState::Finished) {
                follow.continuous.reset();
            }
            if (kEmitter.playing && !follow.continuous) {
                play(follow, *kSound, kAt, true);
            } else if (!kEmitter.playing && follow.continuous) {
                sounds->stop(*follow.continuous, 0.05F);
                follow.continuous.reset();
            } else if (follow.continuous && kAt) {
                sounds->move(*follow.continuous, *kAt);
            }
        }

        // What went away goes as its policy says.
        for (auto at = followed.begin(); at != followed.end();) {
            if (at->second.seen) {
                ++at;
                continue;
            }
            if (const auto kContinuous = at->second.continuous) {
                switch (at->second.despawn) {
                case kDetachToCompletion:
                    // It plays to its end; a loop, which has none, fades as
                    // `fade_out` would.
                    if (!sounds->declaration(at->second.sound)->loop) {
                        break;
                    }
                    [[fallthrough]];
                case kFadeOut:
                    sounds->stop(*kContinuous, 0.25F);
                    break;
                default:
                    sounds->stop(*kContinuous);
                    break;
                }
            }
            at = followed.erase(at);
        }
        sounds->update(seconds);
    }
};

WorldAudio::WorldAudio(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

WorldAudio::~WorldAudio() = default;

result::Result<std::unique_ptr<WorldAudio>>
WorldAudio::create(const schema::SchemaRegistry& registry, audio::Sounds& sounds, WorldAudioSettings settings) {
    RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kEmitter, registry.find(settings.emitter));
    std::optional<schema::ComponentRuntimeId> listener;
    if (settings.listener) {
        RAWFRAME_TRY_ASSIGN(listener, registry.find(*settings.listener));
    }
    if (registry.descriptor(kEmitter).size != sizeof(Emitter) ||
        (listener && registry.descriptor(*listener).size != sizeof(Listener))) {
        return refuse(result::ErrorClass::InvalidArgument,
                      WorldAudioError::BadComponents,
                      "the emitter or listener component is not rawframe.sound's size");
    }
    for (const auto& [kId, kIndex] : settings.sounds) {
        if (sounds.declaration(kIndex) == nullptr) {
            return refuse(
                result::ErrorClass::InvalidArgument, WorldAudioError::BadComponents, "a sound index the Sounds lack");
        }
    }
    auto state = std::make_unique<State>();
    state->sounds = &sounds;
    state->settings = std::move(settings);
    const std::array<world::ColumnTerm, 1> kEmitters = {world::ColumnTerm{kEmitter, world::Access::Read}};
    RAWFRAME_TRY_ASSIGN(state->emitters, world::ColumnQuery::resolve(kEmitters, registry));
    if (listener) {
        const std::array<world::ColumnTerm, 1> kListeners = {world::ColumnTerm{*listener, world::Access::Read}};
        RAWFRAME_TRY_ASSIGN(state->listeners, world::ColumnQuery::resolve(kListeners, registry));
    }
    if (const auto kPose = registry.find(physics2d::Pose2D::kComponentTypeId)) {
        state->pose2d = *kPose;
    }
    if (const auto kPose = registry.find(physics3d::Pose3D::kComponentTypeId)) {
        state->pose3d = *kPose;
    }
    return std::unique_ptr<WorldAudio>{new WorldAudio{std::move(state)}};
}

void WorldAudio::bindListener(std::optional<world::EntityHandle> entity) {
    state_->bound = entity;
}

void WorldAudio::update(world::World& world, float seconds) {
    state_->update(world, seconds);
}

const WorldAudioStatistics& WorldAudio::statistics() const noexcept {
    return state_->statistics;
}

namespace {

/// Whether the program lays `type` out as this module reads it.
bool laidOut(const kest::Program& program,
             std::string_view type,
             std::size_t size,
             std::initializer_list<std::pair<std::string_view, std::size_t>> fields) {
    const auto kLayout = program.layout(type);
    if (!kLayout.has_value() || kLayout->size != size || kLayout->fields.size() != fields.size()) {
        return false;
    }
    std::size_t index = 0;
    for (const auto& [kName, kOffset] : fields) {
        if (kLayout->fields[index].name != kName || kLayout->fields[index].offset != kOffset) {
            return false;
        }
        ++index;
    }
    return true;
}

} // namespace

result::Result<GameAudio> loadGameAudio(const world_kest::GameFiles& game, const kest::Program& program) {
    const world_kest::GameDescription& kDescription = game.description();
    if (!kDescription.audio) {
        return refuse(result::ErrorClass::NotFound, WorldAudioError::NoAudio, "the game declares no mixer");
    }
    GameAudio loaded;
    std::optional<schema::ComponentTypeId> emitter;
    std::optional<schema::ComponentTypeId> listener;
    for (const world_kest::GameComponent& component : kDescription.components) {
        if (component.kestType == "Emitter" || component.kestType == "sound.Emitter") {
            emitter = component.id;
        } else if (component.kestType == "Listener" || component.kestType == "sound.Listener") {
            listener = component.id;
        }
    }
    if (!emitter) {
        return refuse(result::ErrorClass::InvalidArgument,
                      WorldAudioError::BadComponents,
                      "a game with a mixer declares an emitter component");
    }
    if (!laidOut(program,
                 "Emitter",
                 sizeof(Emitter),
                 {{"sound", offsetof(Emitter, sound)},
                  {"cue", offsetof(Emitter, cue)},
                  {"playing", offsetof(Emitter, playing)},
                  {"despawn", offsetof(Emitter, despawn)}}) ||
        (listener && !laidOut(program, "Listener", sizeof(Listener), {{"active", offsetof(Listener, active)}}))) {
        return refuse(result::ErrorClass::InvalidArgument,
                      WorldAudioError::BadComponents,
                      "the program lays out rawframe.sound's types otherwise than this engine reads them");
    }
    loaded.emitter = *emitter;
    loaded.listener = listener;
    RAWFRAME_TRY_ASSIGN(const std::string_view kMixer, game.document(kDescription.audio->mixer));
    auto layout = audio::readLayout(kMixer);
    if (!layout.has_value()) {
        return std::unexpected<result::Error>{std::move(layout).error().withContext("name", kDescription.audio->mixer)};
    }
    loaded.layout = std::move(*layout);
    for (const world_kest::GameSound& sound : kDescription.audio->sounds) {
        RAWFRAME_TRY_ASSIGN(const std::string_view kDeclared, game.document(sound.path));
        auto declaration = audio::readSound(kDeclared, loaded.layout);
        if (!declaration.has_value()) {
            return std::unexpected<result::Error>{std::move(declaration).error().withContext("name", sound.path)};
        }
        loaded.sounds.emplace_back(sound.id, std::move(*declaration));
    }
    return loaded;
}

} // namespace rawframe::world_audio
