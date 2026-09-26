#include "admission.h"
#include "animation_doors.h"
#include "animation_plan.h"
#include "game_files_participant.h"
#include "game_persistence.h"
#include "game_scenes.h"
#include "mod_handlers.h"
#include "mod_services.h"
#include "physics_doors.h"
#include "physics_facts.h"
#include "predictor.h"
#include "rawframe/base/platform.h"
#include "rawframe/base/sha256.h"
#include "rawframe/composition/composition.h"
#include "rawframe/kest/errors.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/physics3d/physics.h"
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
#include "rawframe/world_runtime/save.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>

#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#endif

namespace rawframe::world_kest {

namespace {

constexpr std::string_view kIdentity = "rawframe.world_kest.game";
constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name, kGameFiles.name};
constexpr std::string_view kProvides[] = {world_replication::kReplicationPlan.name,
                                          world_runtime::kCheckpointPlan.name,
                                          world_runtime::kSavePlan.name,
                                          physics2d::kPhysics2DPlan.name,
                                          physics3d::kPhysics3DPlan.name,
                                          world_animation::kAnimationPlan.name};

constexpr diagnostics::EventIdentity kGameLoaded{"world_kest", "game_loaded"};
constexpr diagnostics::EventIdentity kGameReloaded{"world_kest", "game_reloaded"};
constexpr diagnostics::EventIdentity kReloadRefused{"world_kest", "game_reload_refused"};
constexpr diagnostics::EventIdentity kAdmissionFailed{"world_kest", "admission_rule_failed"};
constexpr diagnostics::EventIdentity kKestSummary{"world_kest", "kest_summary"};

#if RAWFRAME_FILE_SYSTEM
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
#endif

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldKestError error, std::string_view why) {
    return result::fail(errorClass, kWorldKestDomain, code(error), why);
}

/// The loaded game. Everything the registry and the systems borrow (names,
/// declarations) lives here, and this participant lives as long as the World.
class GameParticipant final : public composition::Participant,
                              public world_replication::ReplicationPlan,
                              public world_runtime::CheckpointPlan,
                              public world_runtime::SavePlan,
                              public physics2d::Physics2DPlan,
                              public physics3d::Physics3DPlan,
                              public world_animation::AnimationPlan {
public:
    GameParticipant() noexcept = default;

    result::Status load(composition::ParticipantContext& context, const GameFiles& files) {
        const composition::Configuration& configuration = context.configuration();
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(world_runtime::kSimulation));
        files_ = &files;
        timing_ = std::make_unique<KestTiming>(context.clock());
        game_ = files.description();
        RAWFRAME_TRY_ASSIGN(reloadEvery_, configuration.unsignedInteger("kest.reload_every", 0));
        const auto kPlanOnly = configuration.text("kest.plan_only");
        if (kPlanOnly.has_value() && *kPlanOnly != "true" && *kPlanOnly != "false") {
            return refuse(
                result::ErrorClass::InvalidArgument, WorldKestError::UnknownName, "kest.plan_only is true or false");
        }
        planOnly_ = kPlanOnly == "true";
#if RAWFRAME_FILE_SYSTEM
        if (files.directory()) {
            sourcesWritten_ = newestSource(*files.directory());
        }
#endif
        auto program = files.compile(game_.program, {});
        if (!program.has_value()) {
            // The compiler's first diagnostic is on the error already.
            return std::unexpected<result::Error>{std::move(program).error().withContext("program", game_.program)};
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
        const GameScenes kScenes{game_, layouts_};
        RAWFRAME_TRY(kScenes.addScenes(files, game_.spawns, references_));
        RAWFRAME_TRY(kScenes.addModScenes(files, game_.spawns));
        RAWFRAME_TRY(kScenes.addPrefabs(files, prefabs_));
        RAWFRAME_TRY(planReplication(files.digest()));
        for (const content::BuildReference& build : files.modBuilds()) {
            mods_.push_back(world_snapshot::CheckpointMod{.subject = build.subject, .version = build.version});
        }
        RAWFRAME_TRY(planPrediction(configuration));
        RAWFRAME_TRY(planInterest());
        RAWFRAME_TRY(planPhysics());
        if (!planOnly_) {
            RAWFRAME_TRY_ASSIGN(animation_, animationSettings(files, layouts_));
        }
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
        RAWFRAME_TRY_ASSIGN(persistence_, planPersistence(game_, layouts_));
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
        // A system a mod replaces runs the mod's function instead, on the
        // mod's machine (D200).
        std::vector<std::string> replaced;
        for (const GameModProgram& modProgram : files.modPrograms()) {
            for (const ModReplacement& replacement : modProgram.replacements) {
                const auto kPoint = std::ranges::find(game_.mods.points, replacement.point, &GameExtensionPoint::name);
                replaced.push_back(kPoint->accepts);
            }
        }
        std::vector<KestSystemDeclaration> declarations;
        for (std::size_t index = 0; index < game_.systems.size(); ++index) {
            const GameSystem& system = game_.systems[index];
            if (std::ranges::contains(replaced, system.identity)) {
                continue;
            }
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
        RAWFRAME_TRY(addAnimationDoors(doors, &animationDoors_));
        std::vector<std::size_t> sizes;
        for (const kest::TypeLayout& layout : layouts_) {
            sizes.push_back(layout.size);
        }
        modServices_ = std::make_unique<ModServices>(game_, sizes);
        RAWFRAME_TRY(modServices_->addDoors(doors));
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
                                .prefabs = prefabs_,
                                .limits = {.heapBytes = static_cast<std::size_t>(kHeap), .fuelPerCall = kFuel},
                                .systems = declarations,
                                .timing = timing_.get()}));
        if (!game_.admission.empty()) {
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kAdmissionHeap,
                                configuration.unsignedInteger("kest.admission_heap_bytes", 1U << 20U));
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kAdmissionFuel,
                                configuration.unsignedInteger("kest.admission_fuel", 100'000));
            admissionLimits_ = kest::MachineLimits{.heapBytes = static_cast<std::size_t>(kAdmissionHeap),
                                                   .fuelPerCall = kAdmissionFuel};
            RAWFRAME_TRY_ASSIGN(admission_, KestAdmission::create(program_, game_.admission, admissionLimits_));
        }
        // Each taken mod's handlers on a machine of its own (D181).
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kModHeap,
                            configuration.unsignedInteger("kest.mod_heap_bytes", 4U << 20U));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kModFuel,
                            configuration.unsignedInteger("kest.mod_fuel_per_handler", 1'000'000));
        RAWFRAME_TRY_ASSIGN(
            modHandlers_,
            modHandlers(game_,
                        layouts_,
                        files,
                        kest::MachineLimits{.heapBytes = static_cast<std::size_t>(kModHeap), .fuelPerCall = kModFuel},
                        timing_.get()));
        for (const std::unique_ptr<KestSystems>& handlers : modHandlers_) {
            RAWFRAME_TRY(simulation_->addSystems(*handlers));
        }
        RAWFRAME_TRY(modServices_->bind(files.modPrograms(), modHandlers_));
        for (const GameModProgram& modProgram : files.modPrograms()) {
            modHandlerCount_ += modProgram.handlers.size();
            modProviderCount_ += modProgram.providers.size();
            modReplacementCount_ += modProgram.replacements.size();
        }
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
                               diagnostics::field("modHandlers", modHandlerCount_),
                               diagnostics::field("modProviders", modProviderCount_),
                               diagnostics::field("modReplacements", modReplacementCount_),
                               diagnostics::field("modClaimsSetAside", files_->setAside().size()),
                               diagnostics::field("entities", spawned)});
        return {};
    }

    /// SPEC-0013's aggregate script time per World tick, once, at stop.
    void stop() noexcept override {
        if (timing_ == nullptr) {
            return;
        }
        const KestTiming::Summary kSummary = timing_->summary();
        if (kSummary.ticks == 0) {
            return;
        }
        emitter_.log(diagnostics::Severity::Info,
                     kKestSummary,
                     "Kest system time per World tick, in microseconds",
                     {diagnostics::field("ticks", kSummary.ticks),
                      diagnostics::field("p50", kSummary.p50),
                      diagnostics::field("p95", kSummary.p95),
                      diagnostics::field("p99", kSummary.p99),
                      diagnostics::field("max", kSummary.max)});
    }

    /// Between ticks, on the Host thread: when the program's sources changed,
    /// compiles and swaps it in, or says why not and keeps the old one.
    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (systems_ == nullptr || reloadEvery_ == 0 || frame.iteration % reloadEvery_ != 0) {
            return;
        }
        // A reload watches a game's directory; from content, nothing changes.
#if RAWFRAME_FILE_SYSTEM
        if (!files_->directory()) {
            return;
        }
        const auto kWritten = newestSource(*files_->directory());
        if (!kWritten || kWritten == sourcesWritten_) {
            return;
        }
        sourcesWritten_ = kWritten;
#else
        return;
#endif
        std::string report;
        auto program = files_->compile(game_.program, {}, &report);
        // The admission rule follows the program, and only if the systems
        // do: a refused reload keeps both as they were.
        std::unique_ptr<KestAdmission> admission;
        result::Status reloaded;
        if (!program.has_value()) {
            reloaded = std::unexpected<result::Error>{std::move(program).error()};
        } else if (admission_ != nullptr) {
            auto made = KestAdmission::create(*program, game_.admission, admissionLimits_);
            if (made.has_value()) {
                admission = std::move(*made);
            } else {
                reloaded = std::unexpected<result::Error>{std::move(made).error()};
            }
        }
        if (reloaded.has_value()) {
            reloaded = systems_->reload(*program);
        }
        if (reloaded.has_value() && admission != nullptr) {
            admission_ = std::move(admission);
        }
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
    std::optional<network::Reject> admit(const network::Hello& hello) noexcept override {
        if (admission_ == nullptr) {
            return std::nullopt;
        }
        const std::uint64_t kFailures = admission_->failures();
        auto refusal = admission_->admit(hello);
        if (admission_->failures() != kFailures) {
            emitter_.log(diagnostics::Severity::Warning,
                         kAdmissionFailed,
                         "the game's admission rule failed; the client was refused as unavailable",
                         {diagnostics::field("report", std::string_view{admission_->lastReport()})});
        }
        return refusal;
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
    const std::optional<world_animation::AnimationSettings>& animation() const noexcept override {
        return animation_;
    }
    void attach(const world_animation::AnimationQueries* queries) noexcept override {
        animationDoors_.queries = queries;
    }

    result::Result<const world_snapshot::SnapshotProjection*> projection() const override {
        if (persistence_.unwritable.has_value()) {
            return std::unexpected<result::Error>{refuse(result::ErrorClass::Unsupported,
                                                         WorldKestError::UnknownName,
                                                         "a component has a field a checkpoint cannot write")
                                                      .error()
                                                      .withContext("component", persistence_.unwritable->component)
                                                      .withContext("field", persistence_.unwritable->field)};
        }
        return &persistence_.projection;
    }
    world_snapshot::CheckpointIdentity identity() const noexcept override {
        return world_snapshot::CheckpointIdentity{.schema = fingerprint_.bytes, .mods = mods_};
    }

    result::Result<const world_save::SaveDeclaration*> saveDeclaration() const override {
        if (game_.save.document.empty()) {
            return std::unexpected<result::Error>{
                refuse(result::ErrorClass::FailedPrecondition, WorldKestError::BadGameLine, "the game declares no save")
                    .error()};
        }
        return &persistence_.save;
    }
    const world_save::SaveDeclaration* playerSaveDeclaration() const noexcept override {
        return game_.playerSave.document.empty() ? nullptr : &persistence_.playerSave;
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == world_replication::kReplicationPlan.name) {
            return composition::provideAs<world_replication::ReplicationPlan>(*this);
        }
        if (capability == world_runtime::kCheckpointPlan.name) {
            return composition::provideAs<world_runtime::CheckpointPlan>(*this);
        }
        if (capability == world_runtime::kSavePlan.name) {
            return composition::provideAs<world_runtime::SavePlan>(*this);
        }
        if (capability == physics2d::kPhysics2DPlan.name) {
            return composition::provideAs<physics2d::Physics2DPlan>(*this);
        }
        if (capability == physics3d::kPhysics3DPlan.name) {
            return composition::provideAs<physics3d::Physics3DPlan>(*this);
        }
        if (capability == world_animation::kAnimationPlan.name) {
            return composition::provideAs<world_animation::AnimationPlan>(*this);
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
                                            .meshes = files_->meshes(),
                                            .collision = collisionDocument()};
    }

    /// The values one spawn gives each of its components.
    [[nodiscard]] result::Result<SpawnValues> spawnValues(const GameSpawn& spawn) const {
        return GameScenes{game_, layouts_}.spawnValues(spawn);
    }

    /// Physics: the program's physics types must be laid out exactly as the
    /// engine's components are, field by field. A process that plays the
    /// game elsewhere runs no physics.
    result::Status planPhysics() {
        if (!game_.physics.has_value()) {
            return {};
        }
        const PhysicsFacts kFacts = physicsFacts(game_.physics->dimensions);
        const auto kType = [](kest::FieldKind kind) -> std::optional<schema::FieldType> {
            switch (kind) {
            case kest::FieldKind::U8:
                return schema::FieldType::U8;
            case kest::FieldKind::U32:
                return schema::FieldType::U32;
            case kest::FieldKind::U64:
                return schema::FieldType::U64;
            case kest::FieldKind::Bool:
                return schema::FieldType::Bool;
            case kest::FieldKind::F32:
                return schema::FieldType::F32;
            case kest::FieldKind::F64:
                return schema::FieldType::F64;
            default:
                return std::nullopt;
            }
        };
        std::vector<std::pair<const schema::ComponentLayout*, kest::TypeLayout>> checked;
        for (const schema::ComponentLayout& engine : kFacts.components) {
            const GameComponent& component = *componentNamed(engine.name);
            checked.emplace_back(&engine, layouts_[static_cast<std::size_t>(&component - game_.components.data())]);
        }
        // The queries' answers, those the program uses.
        for (const schema::ComponentLayout& answer : kFacts.answers) {
            if (auto layout = program_->layout(answer.scriptType)) {
                checked.emplace_back(&answer, std::move(*layout));
            }
        }
        for (const auto& [kEngine, layout] : checked) {
            const schema::ComponentLayout& engine = *kEngine;
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
#if RAWFRAME_FILE_SYSTEM
    std::optional<std::filesystem::file_time_type> sourcesWritten_;
#endif
    GameDescription game_;
    std::shared_ptr<const kest::Program> program_;
    std::vector<kest::TypeLayout> layouts_;
    std::vector<schema::ComponentDescriptor> descriptors_;
    world_replication::ReplicationTable table_;
    std::vector<schema::ComponentTypeId> playerComponents_;
    std::optional<world_replication::ComponentCodec> input_;
    network::Fingerprint fingerprint_;
    /// The mods the game runs with, as a checkpoint records them.
    std::vector<world_snapshot::CheckpointMod> mods_;
    std::vector<schema::ComponentTypeId> predicted_;
    std::vector<schema::ComponentTypeId> nearby_;
    kest::MachineLimits predictionLimits_;
    kest::MachineLimits admissionLimits_;
    std::unique_ptr<KestAdmission> admission_;
    std::optional<physics2d::Physics2DSettings> predictedPhysics_;
    std::optional<physics3d::Physics3DSettings> predictedPhysics3d_;
    std::vector<SpawnValues> level_;
    std::vector<SceneReference> references_;
    std::vector<KestPrefab> prefabs_;
    /// Each Kest component's entity fields, which its declaration views.
    std::vector<std::vector<std::size_t>> entityOffsets_;
    std::optional<world_replication::InterestSettings> interest_;
    std::vector<schema::ComponentTypeId> interpolated_;
    std::optional<physics2d::Physics2DSettings> physics2d_;
    std::optional<physics3d::Physics3DSettings> physics3d_;
    PhysicsDoorContext doorContext_;
    std::optional<world_animation::AnimationSettings> animation_;
    AnimationDoorContext animationDoors_;
    GamePersistence persistence_;
    std::vector<std::vector<KestColumn>> columns_;
    std::vector<KestComponent> components_;
    std::vector<std::vector<std::string_view>> after_;
    std::vector<std::vector<std::string_view>> before_;
    std::vector<std::vector<std::string_view>> streams_;
    /// Before the machines whose doors name its services.
    std::unique_ptr<ModServices> modServices_;
    /// Every Kest system's time per tick, the game's and its mods' (D210).
    std::unique_ptr<KestTiming> timing_;
    std::unique_ptr<KestSystems> systems_;
    /// Each taken mod's handlers, on its own machine.
    std::vector<std::unique_ptr<KestSystems>> modHandlers_;
    std::size_t modHandlerCount_ = 0;
    std::size_t modProviderCount_ = 0;
    std::size_t modReplacementCount_ = 0;
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
