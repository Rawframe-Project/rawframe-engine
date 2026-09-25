#include "predictor.h"

#include "physics_doors.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/world.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/kest_systems.h"

#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::world_kest {

namespace {

class KestPredictor final : public world_replication::Predictor {
public:
    result::Status build(const PredictorSettings& settings) {
        const GameDescription& game = *settings.game;
        schema::RegistryBuilder builder;
        for (const schema::ComponentDescriptor& descriptor : settings.descriptors) {
            builder.add(descriptor);
        }
        RAWFRAME_TRY_ASSIGN(registry_, builder.freeze());
        world_.emplace(registry_);
        RAWFRAME_TRY_ASSIGN(player_, world_->create());
        const auto kIdOf = [&](std::string_view name) -> std::optional<schema::ComponentTypeId> {
            for (const GameComponent& component : game.components) {
                if (component.name == name) {
                    return component.id;
                }
            }
            return std::nullopt;
        };
        for (const std::string& name : game.player) {
            const schema::ComponentTypeId kId = *kIdOf(name);
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kRuntime, registry_->find(kId));
            std::vector<std::byte> zero(registry_->descriptor(kRuntime).size);
            RAWFRAME_TRY(world_->insertErased(player_, kRuntime, zero.data()));
        }
        RAWFRAME_TRY_ASSIGN(input_, registry_->find(*kIdOf(game.input)));

        // The predicted systems, with storage of their own for everything a
        // declaration views.
        for (const GameComponent& component : game.components) {
            if (settings.program->layout(component.kestType).has_value()) {
                components_.push_back(KestComponent{.component = component.id, .kestType = component.kestType});
            }
        }
        for (const GameSystem& system : game.systems) {
            if (!system.predicted) {
                continue;
            }
            Declared& declared = declared_.emplace_back();
            declared.system = &system;
            for (const GameColumn& column : system.columns) {
                if (column.entities) {
                    declared.columns.push_back(KestColumn{.component = {}, .element = {}, .entities = true});
                    continue;
                }
                for (const GameComponent& component : game.components) {
                    if (component.name == column.component) {
                        declared.columns.push_back(KestColumn{
                            .component = component.id, .element = component.kestType, .access = column.access});
                    }
                }
            }
            declared.after.assign(system.after.begin(), system.after.end());
            declared.before.assign(system.before.begin(), system.before.end());
        }
        std::vector<KestSystemDeclaration> declarations;
        for (const Declared& declared : declared_) {
            declarations.push_back(KestSystemDeclaration{.identity = declared.system->identity,
                                                         .phase = declared.system->phase,
                                                         .entry = declared.system->entry,
                                                         .columns = declared.columns,
                                                         .after = declared.after,
                                                         .before = declared.before,
                                                         .randomStreams = {}});
        }
        kest::DoorTable doors;
        RAWFRAME_TRY(kest::addStandardMath(doors));
        if (settings.physics.has_value()) {
            RAWFRAME_TRY(addPhysicsDoors(doors, &queries_));
        }
        RAWFRAME_TRY_ASSIGN(systems_,
                            KestSystems::create(KestSystemsSettings{.program = settings.program,
                                                                    .doors = std::move(doors),
                                                                    .components = components_,
                                                                    .limits = settings.limits,
                                                                    .systems = declarations}));
        std::vector<world::SystemDeclaration> scheduled;
        RAWFRAME_TRY(systems_->declareSystems(*registry_, scheduled));
        if (settings.physics.has_value()) {
            RAWFRAME_TRY_ASSIGN(physics_, physics2d::Physics2D::create(*settings.physics));
            RAWFRAME_TRY(physics_->declareSystems(*registry_, scheduled));
            queries_ = physics_.get();
            for (const auto& entity : settings.level) {
                RAWFRAME_TRY_ASSIGN(const world::EntityHandle kMade, world_->create());
                for (const auto& [kComponent, kBytes] : entity) {
                    RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kRuntime, registry_->find(kComponent));
                    std::vector<std::byte> value = kBytes;
                    RAWFRAME_TRY(world_->insertErased(kMade, kRuntime, value.data()));
                }
            }
        }
        RAWFRAME_TRY_ASSIGN(world::Schedule schedule, world::Schedule::compile(scheduled, *registry_));
        schedule_.emplace(std::move(schedule));
        return {};
    }

    std::span<const std::byte> get(schema::ComponentTypeId component) const noexcept override {
        const auto kRuntime = registry_->find(component);
        if (!kRuntime.has_value()) {
            return {};
        }
        const auto* value = static_cast<const std::byte*>(std::as_const(*world_).getErased(player_, *kRuntime));
        return value == nullptr ? std::span<const std::byte>{}
                                : std::span{value, registry_->descriptor(*kRuntime).size};
    }

    result::Status set(schema::ComponentTypeId component, std::span<const std::byte> value) override {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kRuntime, registry_->find(component));
        void* const kInto = world_->getErased(player_, kRuntime);
        if (kInto == nullptr || value.size() != registry_->descriptor(kRuntime).size) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kWorldKestDomain,
                                code(WorldKestError::UnknownName),
                                "the player has no such component, or the value is not its size");
        }
        std::memcpy(kInto, value.data(), value.size());
        return {};
    }

    result::Status step(std::span<const std::byte> input) override {
        RAWFRAME_TRY(set(registry_->descriptor(input_).id, input));
        // A system that fails leaves the tick to the server's correction.
        RAWFRAME_TRY_ASSIGN(const world::TickReport kReport,
                            schedule_->runTick(*world_, tick_, *world::TickRate::of(60)));
        if (!kReport.failures.empty()) {
            return result::fail(result::ErrorClass::Internal,
                                kWorldKestDomain,
                                code(WorldKestError::Cancelled),
                                "a predicted system failed");
        }
        return {};
    }

private:
    struct Declared {
        const GameSystem* system = nullptr;
        std::vector<KestColumn> columns;
        std::vector<std::string_view> after;
        std::vector<std::string_view> before;
    };

    std::shared_ptr<const schema::SchemaRegistry> registry_;
    std::optional<world::World> world_;
    world::EntityHandle player_;
    schema::ComponentRuntimeId input_;
    std::vector<KestComponent> components_;
    std::vector<Declared> declared_;
    std::unique_ptr<KestSystems> systems_;
    std::unique_ptr<physics2d::Physics2D> physics_;
    const physics2d::Physics2DQueries* queries_ = nullptr;
    std::optional<world::Schedule> schedule_;
    world::TickIndex tick_;
};

} // namespace

result::Result<std::unique_ptr<world_replication::Predictor>> makePredictor(const PredictorSettings& settings) {
    auto predictor = std::make_unique<KestPredictor>();
    RAWFRAME_TRY(predictor->build(settings));
    return std::unique_ptr<world_replication::Predictor>{std::move(predictor)};
}

} // namespace rawframe::world_kest
