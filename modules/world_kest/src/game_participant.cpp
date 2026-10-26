#include "admission.h"
#include "animation_doors.h"
#include "animation_plan.h"
#include "command_doors.h"
#include "effect_doors.h"
#include "game_debugging.h"
#include "game_files_participant.h"
#include "game_persistence.h"
#include "game_plans.h"
#include "game_scenes.h"
#include "message_doors.h"
#include "mod_handlers.h"
#include "mod_services.h"
#include "navigation.h"
#include "physics_doors.h"
#include "physics_facts.h"
#include "player_doors.h"
#include "predictor.h"
#include "presentation.h"
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
#include "rawframe/world_kest/sound_doors.h"
#include "rawframe/world_replication/perception.h"
#include "rawframe/world_replication/plan.h"
#include "rawframe/world_runtime/checkpoint.h"
#include "rawframe/world_runtime/debugging.h"
#include "rawframe/world_runtime/picking.h"
#include "rawframe/world_runtime/save.h"
#include "scene_follower.h"
#include "sequences.h"
#include "splines.h"
#include "ui_doors.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <span>

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
                                          world_animation::kAnimationPlan.name,
                                          world_runtime::kComponentFields.name,
                                          world_runtime::kPicking.name,
                                          world_runtime::kDebugging.name,
                                          kPresentationPlan.name};
/// A presenting client's machine: its heap, and the fuel of one call.
constexpr kest::MachineLimits kPresentationLimits{.heapBytes = std::size_t{4} << 20U, .fuelPerCall = 1'000'000};

constexpr diagnostics::EventIdentity kGameLoaded{"world_kest", "game_loaded"};
constexpr diagnostics::EventIdentity kGameReloaded{"world_kest", "game_reloaded"};
constexpr diagnostics::EventIdentity kReloadRefused{"world_kest", "game_reload_refused"};
#if RAWFRAME_FILE_SYSTEM
constexpr diagnostics::EventIdentity kSceneFollowed{"world_kest", "scene_followed"};
constexpr diagnostics::EventIdentity kSceneRefused{"world_kest", "scene_refused"};
#endif
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
                              public world_animation::AnimationPlan,
                              public world_runtime::ComponentFields,
                              public world_runtime::Picking,
                              public PresentationPlan {
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
        fieldSets_ = componentFieldSets(game_, layouts_);
        const GameScenes kScenes{game_, layouts_};
        RAWFRAME_TRY(kScenes.addScenes(files, game_.spawns, references_, &sceneRanges_));
        RAWFRAME_TRY(kScenes.addScatters(files, game_.spawns, references_));
        RAWFRAME_TRY(kScenes.addModScenes(files, game_.spawns));
        RAWFRAME_TRY(kScenes.addPrefabs(files, prefabs_));
        RAWFRAME_TRY(planReplication(files.digest()));
        for (const content::BuildReference& build : files.modBuilds()) {
            mods_.push_back(world_snapshot::CheckpointMod{.subject = build.subject, .version = build.version});
        }
        RAWFRAME_TRY(planPrediction(configuration));
        RAWFRAME_TRY_ASSIGN(interest_, interestOf(game_, layouts_));
        RAWFRAME_TRY(planPhysics());
        // A process that plays elsewhere animates the World it mirrors.
        RAWFRAME_TRY_ASSIGN(animation_, animationSettings(files, layouts_));
        RAWFRAME_TRY_ASSIGN(interpolated_,
                            replicatedAmong(game_, game_.interpolated, "an interpolated component replicates"));
        RAWFRAME_TRY_ASSIGN(own_, replicatedAmong(game_, game_.own, "a component kept to its holder replicates"));
        RAWFRAME_TRY_ASSIGN(persistence_, planPersistence(game_, layouts_));
        // Both sides declare the game's message lane, so a process that
        // plays elsewhere measures the messages too. These systems,
        // where they run, run where the World is authoritative: what they
        // send goes out after the tick.
        RAWFRAME_TRY_ASSIGN(messages_, MessageDoors::create(game_, *program_, MessageDoors::Role::Send));
        // And the command lane, by the commands' sizes.
        RAWFRAME_TRY_ASSIGN(commands_, CommandDoors::create(game_, *program_, CommandDoors::Role::Read));
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
            std::vector<schema::ComponentTypeId>& lookups = lookups_.emplace_back();
            for (const std::string& name : system.lookups) {
                lookups.push_back(componentNamed(name)->id);
            }
            std::vector<KestScan>& scans = scans_.emplace_back();
            for (const std::string& name : system.scans) {
                const GameComponent& component = *componentNamed(name);
                scans.push_back(KestScan{.component = component.id, .kestType = component.kestType});
            }
        }
        // A system a mod replaces runs the mod's function instead, on the
        // mod's machine.
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
                                                         .randomStreams = streams_[index],
                                                         .lookups = lookups_[index],
                                                         .scans = scans_[index]});
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
        // A server plays for everyone: it shows no one player.
        RAWFRAME_TRY(addPlayerDoor(doors, nullptr));
        RAWFRAME_TRY(addUiDoors(doors, nullptr));
        RAWFRAME_TRY(addSoundDoors(doors, nullptr));
        std::vector<std::size_t> sizes;
        for (const kest::TypeLayout& layout : layouts_) {
            sizes.push_back(layout.size);
        }
        modServices_ = std::make_unique<ModServices>(game_, sizes);
        RAWFRAME_TRY(modServices_->addDoors(doors));
        // A server presents nothing: its effect doors keep nothing.
        effects_ = std::make_unique<EffectDoors>(game_, false);
        RAWFRAME_TRY(effects_->addDoors(doors));
        RAWFRAME_TRY(messages_->addDoors(doors));
        RAWFRAME_TRY(commands_->addDoors(doors));
        staging_ = {messages_.get(), commands_.get()};
        for (const GameEffect& effect : game_.effects) {
            effectClasses_.push_back(effect.effectClass);
        }
        if (game_.physics.has_value()) {
            RAWFRAME_TRY(addPhysicsDoors(doors, game_.physics->dimensions, &doorContext_));
        }
        // Splines its programs follow, measured from their points.
        RAWFRAME_TRY(planSplines());
        RAWFRAME_TRY(addSplineDoors(doors, splines_.get()));
        // Sequences it plays, and the events they fire.
        RAWFRAME_TRY(planSequences());
        RAWFRAME_TRY(addSequenceDoors(doors, sequences_.get()));
        // The server bakes the navmesh its programs ask the way across.
        if (game_.navigation.has_value()) {
            navmesh_ = std::make_unique<GameNavmesh>(*game_.navigation);
            navmesh_->attach(doorContext_.queries3d);
            RAWFRAME_TRY(addNavigationDoors(doors, navmesh_.get()));
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kHeap, configuration.unsignedInteger("kest.heap_bytes", 64U << 20U));
        // Half a million steps: about 3.5 ms of a spinning loop here, so a
        // system that runs away is stopped within the 8 ms
        // containment with room for a slower machine, and deterministically.
        // The crowd's busiest system takes a few tens of thousands.
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kFuel, configuration.unsignedInteger("kest.fuel_per_system", 500'000));
        RAWFRAME_TRY_ASSIGN(systems_,
                            KestSystems::create(KestSystemsSettings{
                                .program = program_,
                                .doors = std::move(doors),
                                .components = components_,
                                .prefabs = prefabs_,
                                .limits = {.heapBytes = static_cast<std::size_t>(kHeap), .fuelPerCall = kFuel},
                                .systems = declarations,
                                .timing = timing_.get(),
                                .staging = staging_}));
        if (!game_.admission.empty()) {
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kAdmissionHeap,
                                configuration.unsignedInteger("kest.admission_heap_bytes", 1U << 20U));
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kAdmissionFuel,
                                configuration.unsignedInteger("kest.admission_fuel", 100'000));
            admissionLimits_ = kest::MachineLimits{.heapBytes = static_cast<std::size_t>(kAdmissionHeap),
                                                   .fuelPerCall = kAdmissionFuel};
            RAWFRAME_TRY_ASSIGN(admission_, KestAdmission::create(program_, game_.admission, admissionLimits_));
        }
        // Each taken mod's handlers on a machine of its own.
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
        if (navmesh_ != nullptr) {
            RAWFRAME_TRY(simulation_->addSystems(*navmesh_));
        }
        if (splines_ != nullptr) {
            RAWFRAME_TRY(simulation_->addSystems(*splines_));
        }
        if (sequences_ != nullptr) {
            RAWFRAME_TRY(simulation_->addSystems(*sequences_));
        }
        debugging_.attach(systems_->machine());
        // Time a breakpoint holds a tick is not owed.
        debugging_.tellStood(context.clock(), [this](execution::MonotonicDuration stood) {
            simulation_->excuse(stood);
        });
        return simulation_->addSystems(*systems_);
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        context_ = &context;
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
        std::vector<SpawnValues> authored(game_.spawns.size());
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
            authored[index] = spawnedValues;
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
        // Its scenes' districts, streamed as units to clients.
        districts_.held.clear();
        for (const SceneRange& range : sceneRanges_) {
            for (const SceneUnit& unit : range.units) {
                world_replication::StreamedUnit& district =
                    districts_.held.emplace_back(world_replication::StreamedUnit{.persistent = unit.persistent});
                for (const std::size_t kSpawn : unit.spawns) {
                    std::ranges::copy(made[range.first + kSpawn], std::back_inserter(district.entities));
                }
            }
        }
        ++districts_.changed;
        // A game's own scenes are followed where its sources are watched.
        scenes_ = SceneFollower::spawned(*files_, sceneRanges_, made, authored);
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

    /// The aggregate script time per World tick, once, at stop.
    void stop() noexcept override {
        if (navmesh_ != nullptr) {
            navmesh_->report(emitter_);
        }
        if (splines_ != nullptr) {
            emitter_.log(diagnostics::Severity::Info,
                         kSplineSummary,
                         "the splines measured",
                         {diagnostics::field("measured", splines_->measured())});
        }
        if (sequences_ != nullptr) {
            emitter_.log(diagnostics::Severity::Info,
                         kSequenceSummary,
                         "the sequences played",
                         {diagnostics::field("fired", sequences_->fired()),
                          diagnostics::field("unknown", sequences_->unknown())});
        }
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
        // The Kest attribution: what the game's machine and
        // each mod's hold on their heaps.
        if (context_ != nullptr && systems_ != nullptr) {
            std::uint64_t heap = systems_->machine().heapUsed();
            for (const std::unique_ptr<KestSystems>& handlers : modHandlers_) {
                heap += handlers->machine().heapUsed();
            }
            context_->reportMemory(heap);
        }
        // A process that plays elsewhere reloads the program its present
        // systems run, and nothing of the World.
        if ((systems_ == nullptr && !planOnly_) || reloadEvery_ == 0 || frame.iteration % reloadEvery_ != 0) {
            return;
        }
        // A reload watches a game's directory; from content, nothing changes.
#if RAWFRAME_FILE_SYSTEM
        if (!files_->directory()) {
            return;
        }
        if (!planOnly_) {
            followScenes(*files_->directory());
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
        result::Status reloaded;
        if (!program.has_value()) {
            reloaded = std::unexpected<result::Error>{std::move(program).error()};
        } else {
            reloaded = planOnly_ ? keepsShapes(**program) : reloadSystems(*program);
        }
        if (!reloaded.has_value()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kReloadRefused,
                         "a changed Kest program was not reloaded; the running one continues",
                         {diagnostics::field("error", reloaded.error().description()),
                          diagnostics::field("report", std::string_view{report})});
            return;
        }
        // The plan's program from now on, which a client's present systems
        // take (PresentationPlan::program).
        program_ = std::move(*program);
        emitter_.log(diagnostics::Severity::Info, kGameReloaded, "the Kest program was reloaded");
    }

    /// The systems, and the admission rule with them, running `program`;
    /// refused, both stay as they were.
    result::Status reloadSystems(const std::shared_ptr<const kest::Program>& program) {
        std::unique_ptr<KestAdmission> admission;
        if (admission_ != nullptr) {
            RAWFRAME_TRY_ASSIGN(admission, KestAdmission::create(program, game_.admission, admissionLimits_));
        }
        RAWFRAME_TRY(systems_->reload(program));
        debugging_.attach(systems_->machine());
        if (admission != nullptr) {
            admission_ = std::move(admission);
        }
        return {};
    }

    /// Where the game plays elsewhere, whether `program` lays out every
    /// component as the running one does: the mirror holds them, and
    /// replication reads them as the running one laid them out.
    result::Status keepsShapes(const kest::Program& program) const {
        for (const GameComponent& component : game_.components) {
            const auto kOld = program_->layout(component.kestType);
            const auto kNew = program.layout(component.kestType);
            if (kOld.has_value() != kNew.has_value() || (kOld.has_value() && kOld->mark != kNew->mark)) {
                return std::unexpected<result::Error>{refuse(result::ErrorClass::FailedPrecondition,
                                                             WorldKestError::ColumnMismatch,
                                                             "a reloaded program changes the shape of a type the "
                                                             "World holds")
                                                          .error()
                                                          .withContext("type", component.kestType)};
            }
        }
        return {};
    }

#if RAWFRAME_FILE_SYSTEM
    /// Applies to the World what changed in the game's scenes.
    void followScenes(const std::filesystem::path& directory) noexcept {
        std::vector<result::Error> refusals;
        const auto kChanges = scenes_.follow(*simulation_->world(),
                                             GameScenes{game_, layouts_},
                                             *files_,
                                             readScenes(directory, scenes_.paths()),
                                             refusals);
        for (const SceneChanges& changes : kChanges) {
            emitter_.log(diagnostics::Severity::Info,
                         kSceneFollowed,
                         "a changed scene was applied to the World",
                         {diagnostics::field("scene", std::string_view{changes.path}),
                          diagnostics::field("created", changes.created),
                          diagnostics::field("destroyed", changes.destroyed),
                          diagnostics::field("changed", changes.changed)});
        }
        for (const result::Error& refusal : refusals) {
            emitter_.log(diagnostics::Severity::Warning,
                         kSceneRefused,
                         "a changed scene was not applied; the World keeps what it had",
                         {diagnostics::field("error", refusal.description())});
        }
    }
#endif

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
    std::span<const schema::ComponentTypeId> ownComponents() const noexcept override {
        return own_;
    }
    std::size_t messageRecord() const noexcept override {
        return messages_ != nullptr ? messages_->largest() : 0;
    }

    void takeMessages(std::vector<world_replication::PostedMessage>& into) noexcept override {
        if (messages_ != nullptr) {
            messages_->take(into);
        }
    }

    std::span<const std::size_t> commandSizes() const noexcept override {
        return commands_ != nullptr ? commands_->sizes() : std::span<const std::size_t>{};
    }
    void deliverCommands(std::span<const world_replication::ReceivedCommand> commands) noexcept override {
        if (commands_ != nullptr) {
            commands_->deliver(commands);
        }
    }
    void takeTerminations(std::vector<world_replication::PostedTermination>& into) noexcept override {
        if (messages_ != nullptr) {
            messages_->takeTerminations(into);
        }
    }

    std::span<const world_replication::EffectClass> effectClasses() const noexcept override {
        return effectClasses_;
    }
    const std::optional<world_replication::InterestSettings>& interest() const noexcept override {
        return interest_;
    }
    const world_replication::StreamedUnits* streamedUnits() const noexcept override {
        return &districts_;
    }
    std::size_t maximumMapped() const noexcept override {
        return game_.mapped;
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
        if (navmesh_ != nullptr) {
            navmesh_->attach(queries);
        }
    }
    void attach(const world_replication::InterestHistory* history) noexcept override {
        doorContext_.interest = history;
    }
    bool simulated() const noexcept override {
        return !planOnly_;
    }
    const std::optional<world_animation::AnimationSettings>& animation() const noexcept override {
        return animation_;
    }
    void attach(const world_animation::AnimationQueries* queries) noexcept override {
        animationDoors_.queries = queries;
    }
    std::shared_ptr<const kest::Program> program() const noexcept override {
        return program_;
    }

    result::Result<std::unique_ptr<ClientPresentation>> presentation() const override {
        return ClientPresentation::create(PresentationSettings{.program = program_,
                                                               .game = &game_,
                                                               .descriptors = descriptors_,
                                                               .limits = kPresentationLimits,
                                                               .animation = animation_});
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

    std::span<const world_runtime::ComponentFieldSet> fieldSets() const noexcept override {
        return fieldSets_;
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
        if (capability == world_runtime::kComponentFields.name) {
            return composition::provideAs<world_runtime::ComponentFields>(*this);
        }
        if (capability == kPresentationPlan.name) {
            return composition::provideAs<PresentationPlan>(*this);
        }
        if (capability == world_runtime::kPicking.name) {
            return composition::provideAs<world_runtime::Picking>(*this);
        }
        if (capability == world_runtime::kDebugging.name) {
            return composition::provideAs<world_runtime::Debugging>(debugging_);
        }
        return {};
    }

    /// The first 3D body along the ray, and the scene that brought it:
    /// the physics' answer from its last step, and the scenes the
    /// World follows.
    std::optional<world_runtime::Picked> pick(const std::array<double, 3>& origin,
                                              const std::array<double, 3>& toward) const noexcept override {
        if (doorContext_.queries3d == nullptr) {
            return std::nullopt;
        }
        const physics3d::RayHit3D kHit = doorContext_.queries3d->castRay(origin[0],
                                                                         origin[1],
                                                                         origin[2],
                                                                         static_cast<float>(toward[0]),
                                                                         static_cast<float>(toward[1]),
                                                                         static_cast<float>(toward[2]),
                                                                         physics::kEveryClass);
        if (!kHit.hit) {
            return std::nullopt;
        }
        world_runtime::Picked made{.entity = kHit.entity, .point = {kHit.x, kHit.y, kHit.z}};
        if (const auto kSource = scenes_.sourceOf(kHit.entity); kSource.has_value()) {
            made.scene = kSource->first;
            made.source = kSource->second;
        }
        return made;
    }

    std::optional<std::pair<std::string, base::Bits128>>
    authoredAt(world::EntityHandle entity) const noexcept override {
        return scenes_.sourceOf(entity);
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

    /// What a client predicts (`predictionOf`), and the heap of the
    /// machine it predicts on.
    result::Status planPrediction(const composition::Configuration& configuration) {
        if (game_.predicted.empty()) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(PredictionPlan plan, predictionOf(game_, table_, layouts_, *files_));
        predicted_ = std::move(plan.predicted);
        nearby_ = std::move(plan.nearby);
        predictedPhysics_ = std::move(plan.physics2d);
        predictedPhysics3d_ = std::move(plan.physics3d);
        level_ = std::move(plan.level);
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kHeap,
                            configuration.unsignedInteger("kest.prediction_heap_bytes", 4U << 20U));
        predictionLimits_ = kest::MachineLimits{.heapBytes = static_cast<std::size_t>(kHeap), .fuelPerCall = 1'000'000};
        return {};
    }

    /// The values one spawn gives each of its components.
    [[nodiscard]] result::Result<SpawnValues> spawnValues(const GameSpawn& spawn) const {
        return GameScenes{game_, layouts_}.spawnValues(spawn);
    }

    /// Splines: the game's components of the spline types, laid out
    /// as the engine reads them, measured by the pose of its physics.
    result::Status planSplines() {
        GameSplineComponents found{.dimensions =
                                       game_.physics.has_value() ? game_.physics->dimensions : std::uint8_t{3}};
        for (const GameComponent& component : game_.components) {
            if (ofEngineType(component, "rawframe.spline.Point")) {
                found.points.push_back(component.id);
            } else if (ofEngineType(component, "rawframe.spline.Spline")) {
                found.splines.push_back(component.id);
            }
        }
        if (found.points.empty()) {
            return {};
        }
        const bool kPoints = laidOutAs(*program_,
                                       "rawframe.spline.Point",
                                       sizeof(SplinePoint),
                                       {{"spline.slot", offsetof(SplinePoint, spline)},
                                        {"spline.generation", offsetof(SplinePoint, spline) + sizeof(std::uint32_t)},
                                        {"index", offsetof(SplinePoint, index)}});
        const bool kSplines =
            found.splines.empty() || laidOutAs(*program_, "rawframe.spline.Spline", sizeof(bool), {{"closed", 0}});
        if (!game_.physics.has_value() || !kPoints || !kSplines) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             kWorldKestDomain,
                             code(WorldKestError::BadGameLine),
                             "a spline's points stand where their physics poses put them, laid out as "
                             "rawframe.spline's: the game has a physics line and imports the module")
                    .error()};
        }
        splines_ = std::make_unique<GameSplines>(std::move(found));
        return {};
    }

    /// Sequences: the game's components of the sequence types, laid
    /// out as the engine reads them, and its sequences as read.
    result::Status planSequences() {
        GameSequenceSettings found;
        for (const GameComponent& component : game_.components) {
            if (ofEngineType(component, "rawframe.sequence.Player")) {
                found.players.push_back(component.id);
            } else if (ofEngineType(component, "rawframe.sequence.Cast")) {
                found.casts.push_back(component.id);
            }
        }
        if (found.players.empty()) {
            return {};
        }
        const bool kPlayers = laidOutAs(*program_,
                                        "rawframe.sequence.Player",
                                        sizeof(SequencePlayer),
                                        {{"sequence", offsetof(SequencePlayer, sequence)},
                                         {"time", offsetof(SequencePlayer, time)},
                                         {"playing", offsetof(SequencePlayer, playing)},
                                         {"events", offsetof(SequencePlayer, events)}});
        const bool kCasts = found.casts.empty() ||
                            laidOutAs(*program_,
                                      "rawframe.sequence.Cast",
                                      sizeof(SequenceCast),
                                      {{"player.slot", offsetof(SequenceCast, player)},
                                       {"player.generation", offsetof(SequenceCast, player) + sizeof(std::uint32_t)},
                                       {"track", offsetof(SequenceCast, track)}});
        if (!kPlayers || !kCasts) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             kWorldKestDomain,
                             code(WorldKestError::BadGameLine),
                             "a sequence's players and casts are laid out as rawframe.sequence's: the game imports "
                             "the module")
                    .error()};
        }
        for (const GameSequenceResource& each : files_->sequences()) {
            found.sequences.emplace(each.id, each.timeline);
        }
        sequences_ = std::make_unique<GameSequences>(std::move(found));
        return {};
    }

    /// Physics: the program's physics types must be laid out exactly as the
    /// engine's components are, field by field. A process that plays the
    /// game elsewhere runs no physics.
    result::Status planPhysics() {
        if (!game_.physics.has_value()) {
            return {};
        }
        RAWFRAME_TRY(checkPhysicsLayouts(game_, *program_, layouts_));
        if (!planOnly_ && game_.physics->dimensions == 3) {
            physics3d_ = physics3dSettingsOf(game_, files_->meshes());
        } else if (!planOnly_) {
            physics2d_ = physics2dSettingsOf(game_, files_->tilemaps());
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
    composition::ParticipantContext* context_ = nullptr;
    const GameFiles* files_ = nullptr;
    std::uint64_t reloadEvery_ = 0;
    bool planOnly_ = false;
#if RAWFRAME_FILE_SYSTEM
    std::optional<std::filesystem::file_time_type> sourcesWritten_;
#endif
    GameDescription game_;
    std::shared_ptr<const kest::Program> program_;
    std::vector<kest::TypeLayout> layouts_;
    /// The components field by field, for a reader of the World.
    std::vector<world_runtime::ComponentFieldSet> fieldSets_;
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
    std::vector<SceneRange> sceneRanges_;
    /// The districts its scenes spawned, as the replication server reads
    /// them.
    struct Districts final : world_replication::StreamedUnits {
        std::vector<world_replication::StreamedUnit> held;
        std::uint64_t changed = 0;

        std::span<const world_replication::StreamedUnit> units() const noexcept override {
            return held;
        }
        std::uint64_t version() const noexcept override {
            return changed;
        }
    };
    Districts districts_;
    SceneFollower scenes_;
    std::vector<KestPrefab> prefabs_;
    /// Each Kest component's entity fields, which its declaration views.
    std::vector<std::vector<std::size_t>> entityOffsets_;
    std::optional<world_replication::InterestSettings> interest_;
    std::vector<schema::ComponentTypeId> interpolated_;
    std::vector<schema::ComponentTypeId> own_;
    std::optional<physics2d::Physics2DSettings> physics2d_;
    std::optional<physics3d::Physics3DSettings> physics3d_;
    PhysicsDoorContext doorContext_;
    std::unique_ptr<GameNavmesh> navmesh_;
    std::unique_ptr<GameSplines> splines_;
    std::unique_ptr<GameSequences> sequences_;
    std::optional<world_animation::AnimationSettings> animation_;
    AnimationDoorContext animationDoors_;
    GamePersistence persistence_;
    std::vector<std::vector<KestColumn>> columns_;
    std::vector<KestComponent> components_;
    std::vector<std::vector<std::string_view>> after_;
    std::vector<std::vector<std::string_view>> before_;
    std::vector<std::vector<std::string_view>> streams_;
    std::vector<std::vector<schema::ComponentTypeId>> lookups_;
    std::vector<std::vector<KestScan>> scans_;
    /// Before the machines whose doors name its services.
    std::unique_ptr<ModServices> modServices_;
    /// Every Kest system's time per tick, the game's and its mods'.
    std::unique_ptr<KestTiming> timing_;
    std::unique_ptr<EffectDoors> effects_;
    std::unique_ptr<MessageDoors> messages_;
    std::unique_ptr<CommandDoors> commands_;
    std::array<KestStaging*, 2> staging_{};
    std::vector<world_replication::EffectClass> effectClasses_;
    /// Before the systems, so it outlives the machine that calls it.
    GameDebugging debugging_;
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
    registerPresented(registrar);
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
