#include "rawframe/composition/composition.h"
#include "rawframe/kest/errors.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_kest/registrar.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

namespace rawframe::world_kest {

namespace {

constexpr std::string_view kIdentity = "rawframe.world_kest.game";
constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};
constexpr std::size_t kMaximumGameFileBytes = std::size_t{1} << 20U;

constexpr diagnostics::EventIdentity kGameLoaded{"world_kest", "game_loaded"};

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
class GameParticipant final : public composition::Participant {
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
        std::string report;
        auto program = kest::Program::compileFile(kProgram, compile, &report);
        if (!program.has_value()) {
            return std::unexpected<result::Error>{
                std::move(program).error().withContext("path", kProgram).withContext("report", report)};
        }
        program_ = std::move(*program);

        for (const GameComponent& component : game_.components) {
            RAWFRAME_TRY_ASSIGN(kest::TypeLayout layout, program_->layout(component.kestType));
            RAWFRAME_TRY(simulation_->addComponent(schema::ComponentDescriptor{.id = component.id,
                                                                               .name = component.name,
                                                                               .size = layout.size,
                                                                               .alignment = layout.alignment,
                                                                               .plainData = true,
                                                                               .operations = {}}));
            layouts_.push_back(std::move(layout));
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
        }
        std::vector<KestSystemDeclaration> declarations;
        for (std::size_t index = 0; index < game_.systems.size(); ++index) {
            const GameSystem& system = game_.systems[index];
            declarations.push_back(KestSystemDeclaration{.identity = system.identity,
                                                         .phase = system.phase,
                                                         .entry = system.entry,
                                                         .columns = columns_[index],
                                                         .after = after_[index],
                                                         .before = before_[index]});
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
        if (simulation_ == nullptr) {
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

private:
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
    GameDescription game_;
    std::shared_ptr<const kest::Program> program_;
    std::vector<kest::TypeLayout> layouts_;
    std::vector<std::vector<KestColumn>> columns_;
    std::vector<KestComponent> components_;
    std::vector<std::vector<std::string_view>> after_;
    std::vector<std::vector<std::string_view>> before_;
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
        .requiredCapabilities = kNeeds,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "world_kest.game",
        .budgetOwner = "world",
    });
}

} // namespace rawframe::world_kest
