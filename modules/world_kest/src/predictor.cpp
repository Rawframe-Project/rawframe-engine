#include "predictor.h"

#include "physics_doors.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/world.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/kest_systems.h"

#include <cstddef>
#include <cstring>
#include <map>
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
            RAWFRAME_TRY(addPhysicsDoors(doors, 2, &doorContext_));
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
            doorContext_.queries = physics_.get();
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

    void rate(world::TickRate rate) noexcept override {
        rate_ = rate;
    }

    result::Status place(std::span<const world_replication::NeighborValue> values) override {
        // Static bodies are the level's already.
        const auto kStatic = [](const world_replication::NeighborValue& value) {
            return value.component == physics2d::Body2D::kComponentTypeId &&
                   value.value.size() == sizeof(physics2d::Body2D) &&
                   std::to_integer<std::uint8_t>(value.value[offsetof(physics2d::Body2D, motion)]) ==
                       static_cast<std::uint8_t>(physics::Motion::Static);
        };
        std::map<std::uint32_t, world::EntityHandle> kept;
        for (std::size_t first = 0; first < values.size();) {
            std::size_t last = first;
            bool skip = false;
            while (last < values.size() && values[last].entity == values[first].entity) {
                skip = skip || kStatic(values[last]);
                ++last;
            }
            if (!skip) {
                RAWFRAME_TRY(placeOne(values.subspan(first, last - first), kept));
            }
            first = last;
        }
        // Whatever the client no longer mirrors is gone here too.
        for (const auto& [net, entity] : neighbors_) {
            if (!kept.contains(net)) {
                static_cast<void>(world_->destroy(entity));
            }
        }
        neighbors_ = std::move(kept);
        return {};
    }

    result::Status step(std::span<const std::byte> input) override {
        RAWFRAME_TRY(set(registry_->descriptor(input_).id, input));
        // A system that fails leaves the tick to the server's correction.
        RAWFRAME_TRY_ASSIGN(const world::TickReport kReport, schedule_->runTick(*world_, tick_, rate_));
        if (!kReport.failures.empty()) {
            return result::fail(result::ErrorClass::Internal,
                                kWorldKestDomain,
                                code(WorldKestError::Cancelled),
                                "a predicted system failed");
        }
        return {};
    }

private:
    /// One other entity's values, onto the entity that stands for it here.
    result::Status placeOne(std::span<const world_replication::NeighborValue> values,
                            std::map<std::uint32_t, world::EntityHandle>& kept) {
        const std::uint32_t kNet = values.front().entity;
        world::EntityHandle entity;
        if (const auto kFound = neighbors_.find(kNet); kFound != neighbors_.end()) {
            entity = kFound->second;
        } else {
            RAWFRAME_TRY_ASSIGN(entity, world_->create());
        }
        kept[kNet] = entity;
        for (const world_replication::NeighborValue& value : values) {
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kRuntime, registry_->find(value.component));
            if (value.value.size() != registry_->descriptor(kRuntime).size) {
                continue;
            }
            if (void* const kInto = world_->getErased(entity, kRuntime)) {
                std::memcpy(kInto, value.value.data(), value.value.size());
            } else {
                std::vector<std::byte> copy(value.value.begin(), value.value.end());
                RAWFRAME_TRY(world_->insertErased(entity, kRuntime, copy.data()));
            }
        }
        return {};
    }

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
    /// A client knows no one's interest but its own: nothing is gated.
    PhysicsDoorContext doorContext_;
    std::optional<world::Schedule> schedule_;
    world::TickIndex tick_;
    world::TickRate rate_;
    /// The entities standing for other entities the client mirrors.
    std::map<std::uint32_t, world::EntityHandle> neighbors_;
};

} // namespace

result::Result<std::unique_ptr<world_replication::Predictor>> makePredictor(const PredictorSettings& settings) {
    auto predictor = std::make_unique<KestPredictor>();
    RAWFRAME_TRY(predictor->build(settings));
    return std::unique_ptr<world_replication::Predictor>{std::move(predictor)};
}

} // namespace rawframe::world_kest
