#include "game_files_participant.h"
#include "physics_doors.h"
#include "physics_facts.h"
#include "predictor.h"
#include "rawframe/base/sha256.h"
#include "rawframe/composition/composition.h"
#include "rawframe/kest/errors.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/scene/resolve.h"
#include "rawframe/scene/scene.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_kest/layouts.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_kest/replication.h"
#include "rawframe/world_replication/perception.h"
#include "rawframe/world_replication/plan.h"
#include "rawframe/world_runtime/checkpoint.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>

namespace rawframe::world_kest {

namespace {

constexpr std::string_view kIdentity = "rawframe.world_kest.game";
constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name, kGameFiles.name};
constexpr std::string_view kProvides[] = {world_replication::kReplicationPlan.name,
                                          world_runtime::kCheckpointPlan.name,
                                          physics2d::kPhysics2DPlan.name,
                                          physics3d::kPhysics3DPlan.name};

constexpr diagnostics::EventIdentity kGameLoaded{"world_kest", "game_loaded"};
constexpr diagnostics::EventIdentity kGameReloaded{"world_kest", "game_reloaded"};
constexpr diagnostics::EventIdentity kReloadRefused{"world_kest", "game_reload_refused"};

/// The newest modification among the `.kest` files beside the program, or
/// nullopt when the directory cannot be read.
std::optional<std::filesystem::file_time_type> newestSource(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::directory_iterator entries{directory, error};
    if (error) {
        return std::nullopt;
    }
    std::optional<std::filesystem::file_time_type> newest;
    for (const std::filesystem::directory_entry& entry : entries) {
        if (entry.path().extension() != ".kest") {
            continue;
        }
        const auto kWritten = entry.last_write_time(error);
        if (!error && (!newest || kWritten > *newest)) {
            newest = kWritten;
        }
    }
    return newest;
}

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldKestError error, std::string_view why) {
    return result::fail(errorClass, kWorldKestDomain, code(error), why);
}

/// Writes `text` as a value of `kind` at `into`. False when it does not parse
/// or does not fit.
bool writeField(kest::FieldKind kind, std::string_view text, std::byte* into) {
    const auto kParse = [text](auto& value) {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size();
    };
    const auto kStore = [into](auto value) {
        std::memcpy(into, &value, sizeof value);
        return true;
    };
    const auto kWhole = [&]<typename T>(T) {
        T value{};
        return kParse(value) && kStore(value);
    };
    switch (kind) {
    case kest::FieldKind::I8:
        return kWhole(std::int8_t{});
    case kest::FieldKind::I16:
        return kWhole(std::int16_t{});
    case kest::FieldKind::I32:
        return kWhole(std::int32_t{});
    case kest::FieldKind::I64:
        return kWhole(std::int64_t{});
    case kest::FieldKind::U8:
        return kWhole(std::uint8_t{});
    case kest::FieldKind::U16:
        return kWhole(std::uint16_t{});
    case kest::FieldKind::U32:
        return kWhole(std::uint32_t{});
    case kest::FieldKind::U64:
        return kWhole(std::uint64_t{});
    case kest::FieldKind::F32:
        return kWhole(float{});
    case kest::FieldKind::F64:
        return kWhole(double{});
    case kest::FieldKind::Bool:
        // A bool is one byte, nought or one.
        if (text == "true" || text == "false") {
            return kStore(static_cast<std::uint8_t>(text == "true" ? 1 : 0));
        }
        return false;
    case kest::FieldKind::Other:
        return false;
    }
    return false;
}

/// The loaded game. Everything the registry and the systems borrow (names,
/// declarations) lives here, and this participant lives as long as the World.
class GameParticipant final : public composition::Participant,
                              public world_replication::ReplicationPlan,
                              public world_runtime::CheckpointPlan,
                              public physics2d::Physics2DPlan,
                              public physics3d::Physics3DPlan {
public:
    GameParticipant() noexcept = default;

    result::Status load(composition::ParticipantContext& context, const GameFiles& files) {
        const composition::Configuration& configuration = context.configuration();
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(world_runtime::kSimulation));
        files_ = &files;
        game_ = files.description();
        RAWFRAME_TRY_ASSIGN(reloadEvery_, configuration.unsignedInteger("kest.reload_every", 0));
        const auto kPlanOnly = configuration.text("kest.plan_only");
        if (kPlanOnly.has_value() && *kPlanOnly != "true" && *kPlanOnly != "false") {
            return refuse(
                result::ErrorClass::InvalidArgument, WorldKestError::UnknownName, "kest.plan_only is true or false");
        }
        planOnly_ = kPlanOnly == "true";
        if (files.directory()) {
            sourcesWritten_ = newestSource(*files.directory());
        }
        std::string report;
        auto program = files.compile(game_.program, {}, &report);
        if (!program.has_value()) {
            return std::unexpected<result::Error>{
                std::move(program).error().withContext("program", game_.program).withContext("report", report)};
        }
        program_ = std::move(*program);

        for (const GameComponent& component : game_.components) {
            RAWFRAME_TRY_ASSIGN(kest::TypeLayout layout, componentLayout(game_, *program_, component));
            descriptors_.push_back(schema::ComponentDescriptor{.id = component.id,
                                                               .name = component.name,
                                                               .size = layout.size,
                                                               .alignment = layout.alignment,
                                                               .plainData = true,
                                                               .operations = {}});
            if (!planOnly_) {
                RAWFRAME_TRY(simulation_->addComponent(descriptors_.back()));
            }
            layouts_.push_back(std::move(layout));
        }
        RAWFRAME_TRY(addScenes(files));
        RAWFRAME_TRY(planReplication(files.digest()));
        RAWFRAME_TRY(planPrediction(configuration));
        RAWFRAME_TRY(planInterest());
        RAWFRAME_TRY(planPhysics());
        for (const std::string& name : game_.interpolated) {
            if (std::ranges::find(game_.replicated, name) == game_.replicated.end()) {
                return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                             WorldKestError::BadGameLine,
                                                             "an interpolated component replicates")
                                                          .error()
                                                          .withContext("name", name)};
            }
            interpolated_.push_back(componentNamed(name)->id);
        }
        RAWFRAME_TRY(planCheckpoints());
        if (planOnly_) {
            // A process that plays the game elsewhere needs what replicates,
            // not the game running here.
            return {};
        }

        columns_.resize(game_.systems.size());
        for (std::size_t index = 0; index < game_.systems.size(); ++index) {
            const GameSystem& system = game_.systems[index];
            for (const GameColumn& column : system.columns) {
                if (column.entities) {
                    columns_[index].push_back(KestColumn{.component = {}, .element = {}, .entities = true});
                    continue;
                }
                const GameComponent& component = *componentNamed(column.component);
                columns_[index].push_back(
                    KestColumn{.component = component.id, .element = component.kestType, .access = column.access});
            }
            after_.emplace_back(system.after.begin(), system.after.end());
            before_.emplace_back(system.before.begin(), system.before.end());
            streams_.emplace_back(system.randomStreams.begin(), system.randomStreams.end());
        }
        std::vector<KestSystemDeclaration> declarations;
        for (std::size_t index = 0; index < game_.systems.size(); ++index) {
            const GameSystem& system = game_.systems[index];
            declarations.push_back(KestSystemDeclaration{.identity = system.identity,
                                                         .phase = system.phase,
                                                         .entry = system.entry,
                                                         .columns = columns_[index],
                                                         .after = after_[index],
                                                         .before = before_[index],
                                                         .randomStreams = streams_[index]});
        }
        entityOffsets_.reserve(game_.components.size());
        for (const GameComponent& component : game_.components) {
            // What the program never names it cannot insert or remove.
            if (program_->layout(component.kestType).has_value()) {
                const kest::TypeLayout& layout =
                    layouts_[static_cast<std::size_t>(&component - game_.components.data())];
                std::vector<std::size_t>& offsets = entityOffsets_.emplace_back();
                for (const GameEntityField& field : game_.entityFields) {
                    const auto kSlot = std::ranges::find(layout.fields, field.field + ".slot", &kest::Field::name);
                    if (field.component == component.name && kSlot != layout.fields.end()) {
                        offsets.push_back(kSlot->offset);
                    }
                }
                components_.push_back(
                    KestComponent{.component = component.id, .kestType = component.kestType, .entityFields = offsets});
            }
        }
        kest::DoorTable doors;
        RAWFRAME_TRY(kest::addStandardMath(doors));
        if (game_.physics.has_value()) {
            RAWFRAME_TRY(addPhysicsDoors(doors, game_.physics->dimensions, &doorContext_));
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kHeap, configuration.unsignedInteger("kest.heap_bytes", 64U << 20U));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kFuel,
                            configuration.unsignedInteger("kest.fuel_per_system", 10'000'000));
        RAWFRAME_TRY_ASSIGN(systems_,
                            KestSystems::create(KestSystemsSettings{
                                .program = program_,
                                .doors = std::move(doors),
                                .components = components_,
                                .limits = {.heapBytes = static_cast<std::size_t>(kHeap), .fuelPerCall = kFuel},
                                .systems = declarations}));
        return simulation_->addSystems(*systems_);
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (simulation_ == nullptr || planOnly_) {
            return {};
        }
        world::World& world = *simulation_->world();
        // Every entity first, so a scene's references have something to
        // name; then each one's components, references written in.
        std::vector<std::vector<world::EntityHandle>> made(game_.spawns.size());
        for (std::size_t index = 0; index < game_.spawns.size(); ++index) {
            for (std::uint32_t count = 0; count < game_.spawns[index].count; ++count) {
                RAWFRAME_TRY_ASSIGN(const world::EntityHandle kEntity, world.create());
                made[index].push_back(kEntity);
            }
        }
        std::size_t spawned = 0;
        for (std::size_t index = 0; index < game_.spawns.size(); ++index) {
            RAWFRAME_TRY_ASSIGN(SpawnValues spawnedValues, spawnValues(game_.spawns[index]));
            for (const SceneReference& reference : references_) {
                if (reference.spawn != index) {
                    continue;
                }
                const auto kValue =
                    std::ranges::find(spawnedValues, reference.component, &SpawnValues::value_type::first);
                const world::EntityHandle kTarget = made[reference.target].front();
                std::memcpy(kValue->second.data() + reference.slot, &kTarget.slot, sizeof kTarget.slot);
                std::memcpy(
                    kValue->second.data() + reference.generation, &kTarget.generation, sizeof kTarget.generation);
            }
            std::vector<std::pair<schema::ComponentRuntimeId, std::vector<std::byte>>> values;
            for (const auto& [kComponent, kBytes] : spawnedValues) {
                RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, world.registry().find(kComponent));
                values.emplace_back(kId, kBytes);
            }
            for (const world::EntityHandle kEntity : made[index]) {
                for (auto& [id, bytes] : values) {
                    RAWFRAME_TRY(world.insertErased(kEntity, id, bytes.data()));
                }
                ++spawned;
            }
        }
        context.emitter().log(diagnostics::Severity::Info,
                              kGameLoaded,
                              "a Kest game was loaded into the World",
                              {diagnostics::field("components", game_.components.size()),
                               diagnostics::field("systems", game_.systems.size()),
                               diagnostics::field("entities", spawned)});
        return {};
    }

    /// Between ticks, on the Host thread: when the program's sources changed,
    /// compiles and swaps it in, or says why not and keeps the old one.
    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (systems_ == nullptr || reloadEvery_ == 0 || frame.iteration % reloadEvery_ != 0) {
            return;
        }
        if (!files_->directory()) {
            return;
        }
        const auto kWritten = newestSource(*files_->directory());
        if (!kWritten || kWritten == sourcesWritten_) {
            return;
        }
        sourcesWritten_ = kWritten;
        std::string report;
        auto program = files_->compile(game_.program, {}, &report);
        result::Status reloaded = program.has_value()
                                      ? systems_->reload(*program)
                                      : result::Status{std::unexpected<result::Error>{std::move(program).error()}};
        if (!reloaded.has_value()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kReloadRefused,
                         "a changed Kest program was not reloaded; the running one continues",
                         {diagnostics::field("error", reloaded.error().description()),
                          diagnostics::field("report", std::string_view{report})});
            return;
        }
        emitter_.log(diagnostics::Severity::Info, kGameReloaded, "the Kest program was reloaded");
    }

    std::span<const schema::ComponentDescriptor> components() const noexcept override {
        return descriptors_;
    }
    const world_replication::ReplicationTable& table() const noexcept override {
        return table_;
    }
    std::span<const schema::ComponentTypeId> playerComponents() const noexcept override {
        return playerComponents_;
    }
    const std::optional<world_replication::ComponentCodec>& input() const noexcept override {
        return input_;
    }
    network::Fingerprint game() const noexcept override {
        return fingerprint_;
    }

    std::span<const schema::ComponentTypeId> predictedComponents() const noexcept override {
        return predicted_;
    }
    result::Result<std::unique_ptr<world_replication::Predictor>> predictor() const override {
        if (predicted_.empty()) {
            return refuse(result::ErrorClass::Unsupported, WorldKestError::UnknownName, "the game predicts nothing");
        }
        return makePredictor(PredictorSettings{.program = program_,
                                               .game = &game_,
                                               .descriptors = descriptors_,
                                               .limits = predictionLimits_,
                                               .physics = predictedPhysics_,
                                               .physics3d = predictedPhysics3d_,
                                               .level = level_});
    }

    std::span<const schema::ComponentTypeId> nearbyComponents() const noexcept override {
        return nearby_;
    }
    bool perceivedInput() const noexcept override {
        return game_.inputPerceived;
    }
    std::span<const schema::ComponentTypeId> interpolatedComponents() const noexcept override {
        return interpolated_;
    }
    const std::optional<world_replication::InterestSettings>& interest() const noexcept override {
        return interest_;
    }

    const std::optional<physics2d::Physics2DSettings>& physics2d() const noexcept override {
        return physics2d_;
    }
    void attach(const physics2d::Physics2DQueries* queries) noexcept override {
        doorContext_.queries = queries;
    }
    const std::optional<physics3d::Physics3DSettings>& physics3d() const noexcept override {
        return physics3d_;
    }
    void attach(const physics3d::Physics3DQueries* queries) noexcept override {
        doorContext_.queries3d = queries;
    }
    void attach(const world_replication::InterestHistory* history) noexcept override {
        doorContext_.interest = history;
    }

    result::Result<const world_snapshot::SnapshotProjection*> projection() const override {
        if (unwritable_.has_value()) {
            return std::unexpected<result::Error>{refuse(result::ErrorClass::Unsupported,
                                                         WorldKestError::UnknownName,
                                                         "a component has a field a checkpoint cannot write")
                                                      .error()
                                                      .withContext("component", unwritable_->component)
                                                      .withContext("field", unwritable_->field)};
        }
        return &projection_;
    }
    world_snapshot::CheckpointIdentity identity() const noexcept override {
        return world_snapshot::CheckpointIdentity{.schema = fingerprint_.bytes};
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == world_replication::kReplicationPlan.name) {
            return composition::provideAs<world_replication::ReplicationPlan>(*this);
        }
        if (capability == world_runtime::kCheckpointPlan.name) {
            return composition::provideAs<world_runtime::CheckpointPlan>(*this);
        }
        if (capability == physics2d::kPhysics2DPlan.name) {
            return composition::provideAs<physics2d::Physics2DPlan>(*this);
        }
        if (capability == physics3d::kPhysics3DPlan.name) {
            return composition::provideAs<physics3d::Physics3DPlan>(*this);
        }
        return {};
    }

private:
    /// What replicates, from the description; and the game's identity: the
    /// description and its program, hashed together.
    result::Status planReplication(const base::Sha256Digest& game) {
        const auto kCodec = [this](std::string_view name) -> result::Result<world_replication::ComponentCodec> {
            const GameComponent& component = *componentNamed(name);
            const std::size_t kIndex = static_cast<std::size_t>(&component - game_.components.data());
            std::vector<std::string> entities;
            for (const GameEntityField& field : game_.entityFields) {
                if (field.component == component.name) {
                    entities.push_back(field.field);
                }
            }
            return codecFor(component.id, layouts_[kIndex], entities);
        };
        std::vector<world_replication::ComponentCodec> codecs;
        for (const std::string& name : game_.replicated) {
            RAWFRAME_TRY_ASSIGN(world_replication::ComponentCodec codec, kCodec(name));
            codecs.push_back(std::move(codec));
        }
        // Wire order is stable identity order, whatever order the text lists.
        std::sort(codecs.begin(), codecs.end(), [](const auto& left, const auto& right) {
            return left.component < right.component;
        });
        table_.components = std::move(codecs);
        for (const std::string& name : game_.player) {
            playerComponents_.push_back(componentNamed(name)->id);
        }
        if (!game_.input.empty()) {
            RAWFRAME_TRY_ASSIGN(input_, kCodec(game_.input));
        }
        // The game is everything its files are, as peers must share it.
        base::Sha256 hasher;
        hasher.update("rawframe.world_kest.game.v2");
        hasher.update(game);
        fingerprint_.bytes = hasher.finish();
        return {};
    }

    /// What a client predicts, checked against SPEC-0041's rules: only the
    /// player's replicated components, with input to predict from, by
    /// systems that write no other replicated component and draw from no
    /// World stream.
    result::Status planPrediction(const composition::Configuration& configuration) {
        if (game_.predicted.empty()) {
            return {};
        }
        const auto kIn = [](const std::vector<std::string>& names, const std::string& name) {
            return std::ranges::find(names, name) != names.end();
        };
        const auto kRefuse = [](std::string_view why, std::string_view subject) {
            return std::unexpected<result::Error>{
                refuse(result::ErrorClass::InvalidArgument, WorldKestError::BadGameLine, why)
                    .error()
                    .withContext("name", subject)};
        };
        if (game_.input.empty()) {
            return kRefuse("a game that predicts needs input to predict from", "input");
        }
        // An entity a replicated value names is the mirror's on a client and
        // the predictor's own there: never the same bits to compare.
        const auto kNamesEntities = [this](const std::string& name) {
            const schema::ComponentTypeId kId = componentNamed(name)->id;
            return std::ranges::any_of(table_.components, [kId](const world_replication::ComponentCodec& codec) {
                return codec.component == kId && codec.namesEntities();
            });
        };
        for (const std::string& name : game_.predicted) {
            if (!kIn(game_.player, name) || !kIn(game_.replicated, name)) {
                return kRefuse("a predicted component is one of the player's and replicates", name);
            }
            if (kNamesEntities(name)) {
                return kRefuse("a predicted component names no entity", name);
            }
            predicted_.push_back(componentNamed(name)->id);
        }
        for (const GameSystem& system : game_.systems) {
            if (!system.predicted) {
                continue;
            }
            if (!system.randomStreams.empty()) {
                return kRefuse("a predicted system draws from no World stream: a client does not have its state",
                               system.identity);
            }
            for (const GameColumn& column : system.columns) {
                // The moment a player saw is the server's to know: a client
                // stepping its own command does not have it.
                if (column.component == world_replication::Perception::kComponentName) {
                    return kRefuse("a predicted system reads no Perception", system.identity);
                }
                if (column.access == world::Access::Write && kIn(game_.replicated, column.component) &&
                    !kIn(game_.predicted, column.component)) {
                    return kRefuse("a predicted system writes no replicated component that is not predicted",
                                   system.identity);
                }
            }
        }
        // A predicting client steps its player's body among the level's
        // static bodies; everything that moves besides it is the server's.
        if (game_.physics.has_value()) {
            if (game_.physics->dimensions == 3) {
                predictedPhysics3d_ = physics3dSettings();
            } else {
                predictedPhysics_ = physicsSettings();
            }
            const PhysicsFacts kFacts = physicsFacts(game_.physics->dimensions);
            for (const GameSpawn& spawn : game_.spawns) {
                RAWFRAME_TRY_ASSIGN(SpawnValues values, spawnValues(spawn));
                const auto kBody = std::ranges::find(values, kFacts.body, &SpawnValues::value_type::first);
                if (kBody == values.end() || static_cast<std::uint8_t>(kBody->second[kFacts.motion]) !=
                                                 static_cast<std::uint8_t>(physics::Motion::Static)) {
                    continue;
                }
                for (std::uint32_t made = 0; made < spawn.count; ++made) {
                    level_.push_back(values);
                }
            }
        }
        for (const std::string& name : game_.nearby) {
            if (!kIn(game_.replicated, name)) {
                return kRefuse("a nearby component replicates", name);
            }
            if (kNamesEntities(name)) {
                return kRefuse("a nearby component names no entity", name);
            }
            nearby_.push_back(componentNamed(name)->id);
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kHeap,
                            configuration.unsignedInteger("kest.prediction_heap_bytes", 4U << 20U));
        predictionLimits_ = kest::MachineLimits{.heapBytes = static_cast<std::size_t>(kHeap), .fuelPerCall = 1'000'000};
        return {};
    }

    /// Who is sent what: the interest line's coordinate fields, each a
    /// floating-point number of the position component. An entity leaves at
    /// an eighth beyond the radius it entered at.
    result::Status planInterest() {
        if (!game_.interest.has_value()) {
            return {};
        }
        const GameInterest& interest = *game_.interest;
        const GameComponent& component = *componentNamed(interest.component);
        const kest::TypeLayout& layout = layouts_[static_cast<std::size_t>(&component - game_.components.data())];
        world_replication::InterestSettings settings{
            .position = component.id, .axes = {}, .radius = interest.radius, .leaveRadius = interest.radius * 1.125};
        for (const std::string& axis : interest.axes) {
            const auto kField = std::ranges::find(layout.fields, axis, &kest::Field::name);
            if (kField == layout.fields.end() ||
                (kField->kind != kest::FieldKind::F32 && kField->kind != kest::FieldKind::F64)) {
                return std::unexpected<result::Error>{
                    refuse(result::ErrorClass::InvalidArgument,
                           WorldKestError::UnknownName,
                           "an interest line names a field that is not a floating-point number of its component")
                        .error()
                        .withContext("field", axis)};
            }
            settings.axes.push_back(world_replication::WireField{.offset = kField->offset,
                                                                 .kind = kField->kind == kest::FieldKind::F32
                                                                             ? world_replication::WireKind::F32
                                                                             : world_replication::WireKind::F64});
        }
        interest_ = std::move(settings);
        return {};
    }

    /// The game's collision document, by identity.
    [[nodiscard]] physics::CollisionDocument collisionDocument() const {
        physics::CollisionDocument document;
        const auto kId = [this](const std::string& name) {
            return std::ranges::find(game_.collision.classes, name, &GameCollisionClass::name)->id;
        };
        for (const GameCollisionClass& declared : game_.collision.classes) {
            document.classes.push_back({.id = declared.id, .name = declared.name});
        }
        for (const GameCollisionRule& rule : game_.collision.rules) {
            document.rules.push_back({.first = kId(rule.first), .second = kId(rule.second), .rule = rule.rule});
        }
        document.fallback = game_.collision.fallback;
        return document;
    }

    /// The 2D physics the game describes: its world and its collision
    /// document.
    [[nodiscard]] physics2d::Physics2DSettings physicsSettings() const {
        return physics2d::Physics2DSettings{.gravityX = game_.physics->gravityX,
                                            .gravityY = game_.physics->gravityY,
                                            .substeps = game_.physics->substeps,
                                            .collision = collisionDocument()};
    }

    /// The same in three dimensions.
    [[nodiscard]] physics3d::Physics3DSettings physics3dSettings() const {
        return physics3d::Physics3DSettings{.gravityX = game_.physics->gravityX,
                                            .gravityY = game_.physics->gravityY,
                                            .gravityZ = game_.physics->gravityZ,
                                            .substeps = game_.physics->substeps,
                                            .collision = collisionDocument()};
    }

    using SpawnValues = std::vector<std::pair<schema::ComponentTypeId, std::vector<std::byte>>>;

    /// A scene entity's field that names another: the spawn it is written
    /// into, the component and where its entity's slot and generation lie,
    /// and the spawn it names.
    struct SceneReference {
        std::size_t spawn = 0;
        schema::ComponentTypeId component;
        std::size_t slot = 0;
        std::size_t generation = 0;
        std::size_t target = 0;
    };

    /// Where `field` of `component` holds an entity: a field the description
    /// declares with an `entity` line, laid out as rawframe.world's Entity.
    result::Result<SceneReference>
    referenceIn(std::string_view component, std::string_view field, std::string_view scene) const {
        const bool kDeclared = std::ranges::any_of(game_.entityFields, [&](const GameEntityField& declared) {
            return declared.component == component && declared.field == field;
        });
        const GameComponent* const kComponent = componentNamed(component);
        const kest::TypeLayout& layout = layouts_[static_cast<std::size_t>(kComponent - game_.components.data())];
        const auto kPart = [&layout, field](std::string_view part) -> const kest::Field* {
            const std::string kName = std::string{field} + "." + std::string{part};
            const auto kFound = std::ranges::find(layout.fields, kName, &kest::Field::name);
            return kFound != layout.fields.end() && kFound->kind == kest::FieldKind::U32 ? &*kFound : nullptr;
        };
        const kest::Field* const kSlot = kPart("slot");
        const kest::Field* const kGeneration = kPart("generation");
        if (!kDeclared || kSlot == nullptr || kGeneration == nullptr) {
            return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                         WorldKestError::UnknownName,
                                                         "a scene's reference is a field the game declares with an "
                                                         "entity line, holding a rawframe.world Entity")
                                                      .error()
                                                      .withContext("scene", scene)
                                                      .withContext("name", field)};
        }
        return SceneReference{.component = kComponent->id, .slot = kSlot->offset, .generation = kGeneration->offset};
    }

    /// Each scene the description names, as the spawns of its entities, one
    /// each: its components must be the game's, laid out as the scene was
    /// authored against.
    result::Status addScenes(const GameFiles& files) {
        const auto kRefuse =
            [](WorldKestError error, std::string_view why, std::string_view scene, std::string_view name) {
                return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument, error, why)
                                                          .error()
                                                          .withContext("scene", scene)
                                                          .withContext("name", name)};
            };
        for (const std::string& path : game_.scenes) {
            RAWFRAME_TRY_ASSIGN(const std::string_view kText, files.scene(path));
            // Its instances resolved: what spawns is the scene's entities and
            // every entity its instances bring.
            auto read = scene::readScene(kText).and_then([&files](const scene::Scene& authored) {
                return scene::resolveInstances(authored, [&files](base::Bits128 source) {
                    return files.sceneById(source).and_then([](std::string_view text) {
                        return scene::readScene(text);
                    });
                });
            });
            if (!read.has_value()) {
                return std::unexpected<result::Error>{std::move(read).error().withContext("scene", path)};
            }
            for (const scene::SchemaMark& mark : read->schema) {
                const auto kComponent = std::ranges::find(game_.components, mark.component, &GameComponent::name);
                if (kComponent == game_.components.end()) {
                    return kRefuse(WorldKestError::UnknownName,
                                   "a scene names a component the game does not declare",
                                   path,
                                   mark.component);
                }
                const std::size_t kIndex = static_cast<std::size_t>(kComponent - game_.components.begin());
                if (layouts_[kIndex].mark != mark.mark) {
                    return kRefuse(WorldKestError::BadGameLine,
                                   "a scene was authored against another layout of a component",
                                   path,
                                   mark.component);
                }
            }
            // Where each of the scene's entities spawns, by its id.
            std::vector<std::pair<base::Bits128, std::size_t>> spawnOf;
            for (const scene::SceneEntity& entity : read->entities) {
                spawnOf.emplace_back(entity.id, game_.spawns.size() + spawnOf.size());
            }
            for (const scene::SceneEntity& entity : read->entities) {
                GameSpawn spawn{.count = 1, .components = {}};
                for (const scene::SceneComponent& component : entity.components) {
                    GameSpawnComponent part{.component = component.name, .fields = {}};
                    for (const scene::SceneField& field : component.fields) {
                        if (field.value.kind == scene::FieldValue::Kind::Entity) {
                            RAWFRAME_TRY_ASSIGN(SceneReference reference,
                                                referenceIn(component.name, field.name, path));
                            reference.spawn = game_.spawns.size();
                            reference.target =
                                std::ranges::find(spawnOf, field.value.entity, &decltype(spawnOf)::value_type::first)
                                    ->second;
                            references_.push_back(std::move(reference));
                            continue;
                        }
                        part.fields.push_back(GameFieldValue{.field = field.name,
                                                             .value = field.value.kind == scene::FieldValue::Kind::True
                                                                          ? std::string{"true"}
                                                                          : field.value.number});
                    }
                    spawn.components.push_back(std::move(part));
                }
                game_.spawns.push_back(std::move(spawn));
            }
        }
        return {};
    }

    /// The values one spawn line gives each of its components.
    [[nodiscard]] result::Result<SpawnValues> spawnValues(const GameSpawn& spawn) const {
        SpawnValues values;
        for (const GameSpawnComponent& part : spawn.components) {
            const std::size_t kIndex =
                static_cast<std::size_t>(componentNamed(part.component) - game_.components.data());
            const kest::TypeLayout& layout = layouts_[kIndex];
            std::vector<std::byte> bytes(layout.size);
            for (GameFieldValue value : part.fields) {
                // A body's collision class, by name.
                const auto kClass = std::ranges::find(game_.collision.classes, value.value, &GameCollisionClass::name);
                if (game_.physics.has_value() &&
                    game_.components[kIndex].id == physicsFacts(game_.physics->dimensions).body &&
                    value.field == "collisionClass" && kClass != game_.collision.classes.end()) {
                    value.value = std::to_string(kClass->id);
                }
                const kest::Field* field = nullptr;
                for (const kest::Field& candidate : layout.fields) {
                    field = candidate.name == value.field ? &candidate : field;
                }
                if (field == nullptr || !writeField(field->kind, value.value, bytes.data() + field->offset)) {
                    return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                                 WorldKestError::UnknownName,
                                                                 "a spawn names a field its component lacks, or "
                                                                 "gives a value that does not fit it")
                                                              .error()
                                                              .withContext("field", value.field)};
                }
            }
            values.emplace_back(game_.components[kIndex].id, std::move(bytes));
        }
        return values;
    }

    /// Physics: the program's physics types must be laid out exactly as the
    /// engine's components are, field by field. A process that plays the
    /// game elsewhere runs no physics.
    result::Status planPhysics() {
        if (!game_.physics.has_value()) {
            return {};
        }
        const PhysicsFacts kFacts = physicsFacts(game_.physics->dimensions);
        const auto kType = [](kest::FieldKind kind) -> std::optional<physics::FieldType> {
            switch (kind) {
            case kest::FieldKind::U8:
                return physics::FieldType::U8;
            case kest::FieldKind::U32:
                return physics::FieldType::U32;
            case kest::FieldKind::U64:
                return physics::FieldType::U64;
            case kest::FieldKind::Bool:
                return physics::FieldType::Bool;
            case kest::FieldKind::F32:
                return physics::FieldType::F32;
            case kest::FieldKind::F64:
                return physics::FieldType::F64;
            default:
                return std::nullopt;
            }
        };
        std::vector<std::pair<const physics::ComponentLayout*, kest::TypeLayout>> checked;
        for (const physics::ComponentLayout& engine : kFacts.components) {
            const GameComponent& component = *componentNamed(engine.name);
            checked.emplace_back(&engine, layouts_[static_cast<std::size_t>(&component - game_.components.data())]);
        }
        // The queries' answers, those the program uses.
        for (const physics::ComponentLayout& answer : kFacts.answers) {
            if (auto layout = program_->layout(answer.scriptType)) {
                checked.emplace_back(&answer, std::move(*layout));
            }
        }
        for (const auto& [kEngine, layout] : checked) {
            const physics::ComponentLayout& engine = *kEngine;
            bool same = layout.size == engine.size && layout.alignment == engine.alignment &&
                        layout.fields.size() == engine.fields.size();
            for (std::size_t index = 0; same && index < layout.fields.size(); ++index) {
                const kest::Field& field = layout.fields[index];
                same = field.name == engine.fields[index].name && field.offset == engine.fields[index].offset &&
                       kType(field.kind) == engine.fields[index].type;
            }
            if (!same) {
                return std::unexpected<result::Error>{
                    refuse(result::ErrorClass::InvalidArgument,
                           WorldKestError::BadGameLine,
                           "the program's physics type is not laid out as the engine's component; import "
                           "the engine's physics module rather than declaring it")
                        .error()
                        .withContext("type", engine.scriptType)
                        .withContext("module", kFacts.module)};
            }
        }
        if (!planOnly_ && game_.physics->dimensions == 3) {
            physics3d_ = physics3dSettings();
        } else if (!planOnly_) {
            physics2d_ = physicsSettings();
        }
        return {};
    }

    /// What a checkpoint holds: every component, field by field from its
    /// Kest layout, with the `entity` lines' fields as references. A game
    /// with a field a checkpoint cannot write (text, a tagged union) loads
    /// and runs; only checkpoints of it are refused, with the reason.
    result::Status planCheckpoints() {
        for (const GameEntityField& field : game_.entityFields) {
            const GameComponent& component = *componentNamed(field.component);
            const kest::TypeLayout& layout = layouts_[static_cast<std::size_t>(&component - game_.components.data())];
            const auto kPiece = [&](std::string_view suffix) -> const kest::Field* {
                for (const kest::Field& piece : layout.fields) {
                    if (piece.name == field.field + std::string{suffix} && piece.kind == kest::FieldKind::U32) {
                        return &piece;
                    }
                }
                return nullptr;
            };
            const kest::Field* slot = kPiece(".slot");
            const kest::Field* generation = kPiece(".generation");
            if (slot == nullptr || generation == nullptr || generation->offset != slot->offset + 4) {
                return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                             WorldKestError::UnknownName,
                                                             "an entity line names a field that is not an Entity")
                                                          .error()
                                                          .withContext("field", field.field)};
            }
        }
        for (std::size_t index = 0; index < game_.components.size(); ++index) {
            const GameComponent& component = game_.components[index];
            const kest::TypeLayout& layout = layouts_[index];
            world_snapshot::SnapshotComponent projected{.id = component.id, .size = layout.size, .fields = {}};
            for (const kest::Field& piece : layout.fields) {
                const auto kEntity = std::find_if(
                    game_.entityFields.begin(), game_.entityFields.end(), [&](const GameEntityField& each) {
                        return each.component == component.name &&
                               (piece.name == each.field + ".slot" || piece.name == each.field + ".generation");
                    });
                if (kEntity != game_.entityFields.end()) {
                    if (piece.name.ends_with(".slot")) {
                        projected.fields.push_back({piece.offset, world_snapshot::FieldKind::Entity});
                    }
                    continue;
                }
                const std::optional<world_snapshot::FieldKind> kKind = snapshotKind(piece.kind);
                if (!kKind.has_value()) {
                    unwritable_ = GameEntityField{.component = component.name, .field = piece.name};
                    return {};
                }
                projected.fields.push_back({piece.offset, *kKind});
            }
            projection_.components.push_back(std::move(projected));
        }
        return {};
    }

    static std::optional<world_snapshot::FieldKind> snapshotKind(kest::FieldKind kind) noexcept {
        switch (kind) {
        case kest::FieldKind::I8:
            return world_snapshot::FieldKind::I8;
        case kest::FieldKind::I16:
            return world_snapshot::FieldKind::I16;
        case kest::FieldKind::I32:
            return world_snapshot::FieldKind::I32;
        case kest::FieldKind::I64:
            return world_snapshot::FieldKind::I64;
        case kest::FieldKind::U8:
            return world_snapshot::FieldKind::U8;
        case kest::FieldKind::U16:
            return world_snapshot::FieldKind::U16;
        case kest::FieldKind::U32:
            return world_snapshot::FieldKind::U32;
        case kest::FieldKind::U64:
            return world_snapshot::FieldKind::U64;
        case kest::FieldKind::F32:
            return world_snapshot::FieldKind::F32;
        case kest::FieldKind::F64:
            return world_snapshot::FieldKind::F64;
        case kest::FieldKind::Bool:
            return world_snapshot::FieldKind::Bool;
        case kest::FieldKind::Other:
            return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] const GameComponent* componentNamed(std::string_view name) const noexcept {
        for (const GameComponent& component : game_.components) {
            if (component.name == name) {
                return &component;
            }
        }
        // parseGame has checked every name, so this is not reached.
        return game_.components.data();
    }

    world_runtime::Simulation* simulation_ = nullptr;
    diagnostics::Emitter emitter_;
    const GameFiles* files_ = nullptr;
    std::uint64_t reloadEvery_ = 0;
    bool planOnly_ = false;
    std::optional<std::filesystem::file_time_type> sourcesWritten_;
    GameDescription game_;
    std::shared_ptr<const kest::Program> program_;
    std::vector<kest::TypeLayout> layouts_;
    std::vector<schema::ComponentDescriptor> descriptors_;
    world_replication::ReplicationTable table_;
    std::vector<schema::ComponentTypeId> playerComponents_;
    std::optional<world_replication::ComponentCodec> input_;
    network::Fingerprint fingerprint_;
    std::vector<schema::ComponentTypeId> predicted_;
    std::vector<schema::ComponentTypeId> nearby_;
    kest::MachineLimits predictionLimits_;
    std::optional<physics2d::Physics2DSettings> predictedPhysics_;
    std::optional<physics3d::Physics3DSettings> predictedPhysics3d_;
    std::vector<SpawnValues> level_;
    std::vector<SceneReference> references_;
    /// Each Kest component's entity fields, which its declaration views.
    std::vector<std::vector<std::size_t>> entityOffsets_;
    std::optional<world_replication::InterestSettings> interest_;
    std::vector<schema::ComponentTypeId> interpolated_;
    std::optional<physics2d::Physics2DSettings> physics2d_;
    std::optional<physics3d::Physics3DSettings> physics3d_;
    PhysicsDoorContext doorContext_;
    world_snapshot::SnapshotProjection projection_;
    /// A field no checkpoint can write, which refuses checkpoints of this game.
    std::optional<GameEntityField> unwritable_;
    std::vector<std::vector<KestColumn>> columns_;
    std::vector<KestComponent> components_;
    std::vector<std::vector<std::string_view>> after_;
    std::vector<std::vector<std::string_view>> before_;
    std::vector<std::vector<std::string_view>> streams_;
    std::unique_ptr<KestSystems> systems_;
};

result::Result<composition::ParticipantOwner> makeGame(composition::ParticipantContext& context) noexcept {
    auto game = std::make_unique<GameParticipant>();
    RAWFRAME_TRY_ASSIGN(const GameFiles* files, context.capability(kGameFiles));
    if (files->named()) {
        RAWFRAME_TRY(game->load(context, *files));
    }
    return composition::ParticipantOwner{game.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registerGameFiles(registrar);
    registrar.submit(composition::ParticipantDeclaration{
        .identity = kIdentity,
        .factory = &makeGame,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvides,
        .requiredCapabilities = kNeeds,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "world_kest.game",
        .budgetOwner = "world",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Maintenance),
    });
}

} // namespace rawframe::world_kest
