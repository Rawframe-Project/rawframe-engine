#include "doorway.h"

#include <algorithm>
#include <cstring>

namespace rawframe::world_kest {

void createDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (doorway.context == nullptr) {
        call.fail("entities are created only while a system runs");
        return;
    }
    auto pending = doorway.context->commands.create();
    if (!pending.has_value()) {
        call.fail("the system's command buffer is full");
        return;
    }
    const world::EntityHandle kEntity = encodePending(*pending, doorway.run);
    static_cast<void>(call.answerValue(std::as_bytes(std::span{&kEntity, 1})));
}

void spawnDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (doorway.context == nullptr) {
        call.fail("prefabs are spawned only while a system runs");
        return;
    }
    const auto kId = static_cast<std::uint64_t>(call.integer(0));
    const auto kPrefab = std::ranges::find(doorway.prefabs, kId, [](const Doorway::Prefab& each) {
        return each.prefab.id;
    });
    if (kPrefab == doorway.prefabs.end()) {
        call.fail("the game declares no prefab of that identity");
        return;
    }
    world::CommandBuffer& commands = doorway.context->commands;
    std::vector<world::PendingEntity>& made = doorway.prefabMade;
    made.clear();
    for (std::size_t count = 0; count < kPrefab->prefab.entities.size(); ++count) {
        auto pending = commands.create();
        if (!pending.has_value()) {
            call.fail("the system's command buffer is full");
            return;
        }
        made.push_back(*pending);
    }
    std::vector<std::size_t>& offsets = doorway.prefabOffsets;
    for (std::size_t entity = 0; entity < made.size(); ++entity) {
        const std::vector<KestPrefab::Part>& parts = kPrefab->prefab.entities[entity].parts;
        for (std::size_t part = 0; part < parts.size(); ++part) {
            doorway.prefabScratch.assign(parts[part].value.begin(), parts[part].value.end());
            offsets.clear();
            for (const KestPrefab::Reference& reference : parts[part].references) {
                const world::EntityHandle kNamed = world::pendingReference(made[reference.target]);
                std::memcpy(doorway.prefabScratch.data() + reference.offset, &kNamed, sizeof kNamed);
                offsets.push_back(reference.offset);
            }
            if (!commands
                     .insertBytes(made[entity],
                                  kPrefab->runtimes[entity][part],
                                  *kPrefab->descriptors[entity][part],
                                  doorway.prefabScratch,
                                  offsets)
                     .has_value()) {
                call.fail("the system's command buffer is full");
                return;
            }
        }
    }
    // The prefab's first entity, as a program holds one this run creates.
    const world::EntityHandle kFirst = made.empty() ? world::EntityHandle{} : encodePending(made.front(), doorway.run);
    static_cast<void>(call.answerValue(std::as_bytes(std::span{&kFirst, 1})));
}

void destroyDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (doorway.context == nullptr) {
        call.fail("entities are destroyed only while a system runs");
        return;
    }
    const auto kTarget = doorway.target(call, 0);
    if (!kTarget) {
        return;
    }
    const auto* kLive = std::get_if<world::EntityHandle>(&*kTarget);
    if (kLive == nullptr) {
        call.fail("an entity is destroyed once it exists, after the barrier that creates it");
        return;
    }
    if (!doorway.context->commands.destroy(*kLive).has_value()) {
        call.fail("the system's command buffer is full");
    }
}

namespace {

/// The stream a draw names, or null once the call has been failed.
world::Pcg32* streamOf(kest::DoorCall& call, Doorway& doorway) {
    if (doorway.context == nullptr) {
        call.fail("random streams are drawn from only while a system runs");
        return nullptr;
    }
    const std::int64_t kIndex = call.integer(0);
    const auto kStreams = doorway.context->declaredStreams;
    if (kIndex < 0 || static_cast<std::uint64_t>(kIndex) >= kStreams.size()) {
        call.fail("the system declared no random stream at that place");
        return nullptr;
    }
    auto stream = doorway.context->random(kStreams[static_cast<std::size_t>(kIndex)]);
    return stream.has_value() ? *stream : nullptr;
}

} // namespace

void belowDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    world::Pcg32* const kStream = streamOf(call, doorway);
    if (kStream == nullptr) {
        return;
    }
    const auto kBound = static_cast<std::uint32_t>(call.integer(1));
    if (kBound == 0) {
        call.fail("a draw below nought has nothing to draw");
        return;
    }
    call.answerInteger(kStream->nextBelow(kBound));
}

void unitDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (world::Pcg32* const kStream = streamOf(call, doorway)) {
        call.answerReal(kStream->nextDouble());
    }
}

void insertDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway::Component& component = *static_cast<Doorway::Component*>(context);
    Doorway& doorway = *component.doorway;
    if (doorway.context == nullptr || component.descriptor == nullptr) {
        call.fail("components are inserted only while a system runs");
        return;
    }
    const auto kTarget = doorway.target(call, 0);
    if (!kTarget) {
        return;
    }
    if (!call.value(1, component.scratch)) {
        call.fail("a component value did not cross as its type");
        return;
    }
    // An entity this run creates, held in the value, as the buffer names it.
    for (const std::size_t kOffset : component.entityFields) {
        world::EntityHandle held;
        std::memcpy(&held, component.scratch.data() + kOffset, sizeof held);
        if (!held.isNull() || held.slot == 0) {
            continue;
        }
        const std::uint32_t kIndex = held.slot & kPendingIndexMask;
        if (kIndex == 0 || (held.slot >> kPendingIndexBits) != (doorway.run & kRunMask)) {
            call.fail("a value holds an entity created by another system run and kept");
            return;
        }
        const world::EntityHandle kPending = world::pendingReference(world::PendingEntity{kIndex - 1U});
        std::memcpy(component.scratch.data() + kOffset, &kPending, sizeof kPending);
    }
    if (!doorway.context->commands
             .insertBytes(*kTarget, component.runtime, *component.descriptor, component.scratch, component.entityFields)
             .has_value()) {
        call.fail("the system's command buffer is full, or a value names an entity the run has not created");
    }
}

void removeDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway::Component& component = *static_cast<Doorway::Component*>(context);
    Doorway& doorway = *component.doorway;
    if (doorway.context == nullptr || component.descriptor == nullptr) {
        call.fail("components are removed only while a system runs");
        return;
    }
    const auto kTarget = doorway.target(call, 0);
    if (kTarget && !doorway.context->commands.removeErased(*kTarget, component.runtime).has_value()) {
        call.fail("the system's command buffer is full");
    }
}

namespace {

/// Whether the running system may look `component` up; false, with the
/// call failed, while no system runs or when the running one never
/// declared the lookup.
bool mayLookUp(kest::DoorCall& call, const Doorway::Component& component) noexcept {
    const Doorway& doorway = *component.doorway;
    if (doorway.context == nullptr) {
        call.fail("components are looked up only while a system runs");
        return false;
    }
    if (component.descriptor == nullptr || !std::ranges::contains(doorway.lookups, component.runtime)) {
        call.fail("the running system does not look this component up: its line has no `lookup` of it");
        return false;
    }
    return true;
}

} // namespace

namespace {

/// Where `component` of the entity a call names lies in the World, null
/// when the entity has none (gone, or created by this run and so without
/// values yet); false when the call failed (`mayLookUp`).
bool lookUp(kest::DoorCall& call, const Doorway::Component& component, const void*& value) noexcept {
    const Doorway& doorway = *component.doorway;
    if (!mayLookUp(call, component)) {
        return false;
    }
    world::EntityHandle entity;
    if (!call.value(0, std::as_writable_bytes(std::span{&entity, 1}))) {
        call.fail("an entity did not cross as an entity");
        return false;
    }
    value = doorway.context->world.getErased(entity, component.runtime);
    return true;
}

} // namespace

void getDoor(kest::DoorCall& call, void* context) noexcept {
    const auto& component = *static_cast<const Doorway::Component*>(context);
    const void* value = nullptr;
    if (!lookUp(call, component, value)) {
        return;
    }
    if (value == nullptr) {
        call.fail("the entity has no such component: ask `has` first");
        return;
    }
    if (!call.answerValue(std::span{static_cast<const std::byte*>(value), component.descriptor->size})) {
        call.fail("the component's value did not cross as its Kest type");
    }
}

void hasDoor(kest::DoorCall& call, void* context) noexcept {
    const auto& component = *static_cast<const Doorway::Component*>(context);
    const void* value = nullptr;
    if (lookUp(call, component, value)) {
        call.answerBoolean(value != nullptr);
    }
}

// The entities holding a looked-up component, in World order (D393): no
// structure changes during a run, so a count and the places below it name
// the same entities until the run ends.
void countDoor(kest::DoorCall& call, void* context) noexcept {
    auto& component = *static_cast<Doorway::Component*>(context);
    if (!mayLookUp(call, component)) {
        return;
    }
    const std::size_t kCount = component.holders->count(component.doorway->context->world);
    call.answerInteger(
        static_cast<std::int64_t>(std::min<std::size_t>(kCount, std::numeric_limits<std::int32_t>::max())));
}

void entityDoor(kest::DoorCall& call, void* context) noexcept {
    auto& component = *static_cast<Doorway::Component*>(context);
    if (!mayLookUp(call, component)) {
        return;
    }
    // Past the last, or below the first, the null entity.
    world::EntityHandle found;
    const std::int64_t kIndex = call.integer(0);
    if (kIndex >= 0) {
        auto left = static_cast<std::uint64_t>(kIndex);
        component.holders->forEachChunk(component.doorway->context->world, [&](const world::ColumnChunk& chunk) {
            if (found.isNull() && left < chunk.entities.size()) {
                found = chunk.entities[left];
            }
            left -= std::min<std::uint64_t>(left, chunk.entities.size());
        });
    }
    if (!call.answerValue(std::as_bytes(std::span{&found, 1}))) {
        call.fail("an entity did not cross as an entity");
    }
}

} // namespace rawframe::world_kest
