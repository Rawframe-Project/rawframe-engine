#include "predictor.h"
#include "rawframe/base/sha256.h"
#include "rawframe/composition/composition.h"
#include "rawframe/kest/errors.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_kest/replication.h"
#include "rawframe/world_replication/plan.h"
#include "rawframe/world_runtime/checkpoint.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>

namespace rawframe::world_kest {

namespace {

constexpr std::string_view kIdentity = "rawframe.world_kest.game";
constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kProvides[] = {world_replication::kReplicationPlan.name,
                                          world_runtime::kCheckpointPlan.name};
constexpr std::size_t kMaximumGameFileBytes = std::size_t{1} << 20U;

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

result::Result<std::string> readFile(const std::string& path) {
    std::FILE* const kFile = std::fopen(path.c_str(), "rb");
    if (kFile == nullptr) {
        return std::unexpected<result::Error>{
            refuse(result::ErrorClass::NotFound, WorldKestError::UnreadableFile, "a game file could not be opened")
                .error()
                .withContext("path", path)};
    }
    std::string text;
    char chunk[4096];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof chunk, kFile)) != 0 && text.size() <= kMaximumGameFileBytes) {
        text.append(chunk, got);
    }
    std::fclose(kFile);
    if (text.size() > kMaximumGameFileBytes) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      WorldKestError::UnreadableFile,
                      "a game description is larger than 1 MiB");
    }
    return text;
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
                              public world_runtime::CheckpointPlan {
public:
    GameParticipant() noexcept = default;

    result::Status load(composition::ParticipantContext& context, const std::string& path) {
        const composition::Configuration& configuration = context.configuration();
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(world_runtime::kSimulation));
        RAWFRAME_TRY_ASSIGN(const std::string kText, readFile(path));
        RAWFRAME_TRY_ASSIGN(game_, parseGame(kText));

        kest::CompileSettings compile;
        if (const auto kLibrary = configuration.text("kest.library")) {
            compile.library = std::string{*kLibrary};
        }
        const std::string kProgram = (std::filesystem::path{path}.parent_path() / game_.program).string();
        programPath_ = kProgram;
        compile_ = compile;
        RAWFRAME_TRY_ASSIGN(reloadEvery_, configuration.unsignedInteger("kest.reload_every", 0));
        const auto kPlanOnly = configuration.text("kest.plan_only");
        if (kPlanOnly.has_value() && *kPlanOnly != "true" && *kPlanOnly != "false") {
            return refuse(
                result::ErrorClass::InvalidArgument, WorldKestError::UnknownName, "kest.plan_only is true or false");
        }
        planOnly_ = kPlanOnly == "true";
        sourcesWritten_ = newestSource(std::filesystem::path{kProgram}.parent_path());
        std::string report;
        auto program = kest::Program::compileFile(kProgram, compile, &report);
        if (!program.has_value()) {
            return std::unexpected<result::Error>{
                std::move(program).error().withContext("path", kProgram).withContext("report", report)};
        }
        program_ = std::move(*program);

        for (const GameComponent& component : game_.components) {
            RAWFRAME_TRY_ASSIGN(kest::TypeLayout layout, program_->layout(component.kestType));
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
        RAWFRAME_TRY(planReplication(kText, kProgram));
        RAWFRAME_TRY(planPrediction(configuration));
        RAWFRAME_TRY(planInterest());
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
        for (const GameComponent& component : game_.components) {
            components_.push_back(KestComponent{.component = component.id, .kestType = component.kestType});
        }
        kest::DoorTable doors;
        RAWFRAME_TRY(kest::addStandardMath(doors));
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
        std::size_t spawned = 0;
        for (const GameSpawn& spawn : game_.spawns) {
            std::vector<std::pair<schema::ComponentRuntimeId, std::vector<std::byte>>> values;
            for (const GameSpawnComponent& part : spawn.components) {
                const std::size_t kIndex =
                    static_cast<std::size_t>(componentNamed(part.component) - game_.components.data());
                const kest::TypeLayout& layout = layouts_[kIndex];
                RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId,
                                    world.registry().find(game_.components[kIndex].id));
                std::vector<std::byte> bytes(layout.size);
                for (const GameFieldValue& value : part.fields) {
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
                values.emplace_back(kId, std::move(bytes));
            }
            for (std::uint32_t made = 0; made < spawn.count; ++made) {
                RAWFRAME_TRY_ASSIGN(const world::EntityHandle kEntity, world.create());
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
        const auto kWritten = newestSource(std::filesystem::path{programPath_}.parent_path());
        if (!kWritten || kWritten == sourcesWritten_) {
            return;
        }
        sourcesWritten_ = kWritten;
        std::string report;
        auto program = kest::Program::compileFile(programPath_, compile_, &report);
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
        return makePredictor(PredictorSettings{
            .program = program_, .game = &game_, .descriptors = descriptors_, .limits = predictionLimits_});
    }

    std::span<const schema::ComponentTypeId> interpolatedComponents() const noexcept override {
        return interpolated_;
    }
    const std::optional<world_replication::InterestSettings>& interest() const noexcept override {
        return interest_;
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
        return {};
    }

private:
    /// What replicates, from the description; and the game's identity: the
    /// description and its program, hashed together.
    result::Status planReplication(std::string_view description, const std::string& programPath) {
        const auto kCodec = [this](std::string_view name) -> result::Result<world_replication::ComponentCodec> {
            const GameComponent& component = *componentNamed(name);
            const std::size_t kIndex = static_cast<std::size_t>(&component - game_.components.data());
            return codecFor(component.id, layouts_[kIndex]);
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
        RAWFRAME_TRY_ASSIGN(const std::string kProgramText, readFile(programPath));
        base::Sha256 hasher;
        hasher.update("rawframe.world_kest.game.v1");
        hasher.update(description);
        hasher.update(std::string_view{"\0", 1});
        hasher.update(kProgramText);
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
        for (const std::string& name : game_.predicted) {
            if (!kIn(game_.player, name) || !kIn(game_.replicated, name)) {
                return kRefuse("a predicted component is one of the player's and replicates", name);
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
                if (column.access == world::Access::Write && kIn(game_.replicated, column.component) &&
                    !kIn(game_.predicted, column.component)) {
                    return kRefuse("a predicted system writes no replicated component that is not predicted",
                                   system.identity);
                }
            }
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
    std::string programPath_;
    kest::CompileSettings compile_;
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
    kest::MachineLimits predictionLimits_;
    std::optional<world_replication::InterestSettings> interest_;
    std::vector<schema::ComponentTypeId> interpolated_;
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
    if (const auto kPath = context.configuration().text("kest.game")) {
        RAWFRAME_TRY(game->load(context, std::string{*kPath}));
    }
    return composition::ParticipantOwner{game.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
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
