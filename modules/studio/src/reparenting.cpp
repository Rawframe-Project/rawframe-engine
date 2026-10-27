#include "hierarchy.h"
#include "shell.h"

#include <cmath>
#include <utility>

namespace rawframe::studio {

namespace {

/// The game's attachment, the one parenting there is: the 3D one
/// where a game has it, else the 2D one.
constexpr std::string_view kAttachments[] = {"rawframe.physics3d.attach", "rawframe.physics2d.attach"};

} // namespace

void ShellParticipant::dropEntity() {
    const std::optional<std::size_t> kHeld = std::exchange(heldEntity_, std::nullopt);
    if (!kHeld.has_value() || *kHeld >= entities_.size() || pointing_ == nullptr) {
        return;
    }
    // Where it was let go, not where the pointer is now.
    const std::optional<std::array<float, 2>> kPointer = releasedAt_.has_value() ? releasedAt_ : pointerLaidOut();
    if (!kPointer.has_value() ||
        std::hypot((*kPointer)[0] - pressedAt_[0], (*kPointer)[1] - pressedAt_[1]) < kDragAfter) {
        return;
    }
    // A clip's items have no parents: a mark dragged along its timeline
    // moves its key, event, or marker in time.
    if (held_.timelineScale > 0) {
        if (std::exchange(held_.planPressed, false)) {
            dropKey(*kHeld, (*kPointer)[0] - pressedAt_[0]);
        }
        return;
    }
    // A blockout's solids have no parents: a footprint dragged in its plan
    // moves its solid, and a row dragged does nothing.
    if (chosen(Listed::Blockout)) {
        if (std::exchange(held_.planPressed, false)) {
            dropSolid(*kHeld, {(*kPointer)[0] - pressedAt_[0], (*kPointer)[1] - pressedAt_[1]});
        }
        return;
    }
    const auto kHit = tree_->hit(root_, (*kPointer)[0], (*kPointer)[1]);
    if (!kHit.has_value() || !kHit->node.has_value()) {
        return;
    }
    // Onto another entity, it follows that one; onto the Hierarchy's own
    // space below its entities, it follows none.
    const auto kOnto = std::ranges::find(entityRows_, *kHit->node);
    if (kOnto != entityRows_.end()) {
        const auto kTarget = static_cast<std::size_t>(kOnto - entityRows_.begin());
        if (kTarget != *kHeld) {
            reparent(*kHeld, kTarget);
        }
    } else if (*kHit->node == entitiesColumn_) {
        reparent(*kHeld, std::nullopt);
    }
}

void ShellParticipant::reparent(std::size_t entity, std::optional<std::size_t> parent) {
    const std::string kEntity = entities_[entity];
    const std::string kName = names_[entity].empty() ? kEntity.substr(0, 8) : names_[entity];
    if (brought_[entity]) {
        refuse("an instance's entity follows what its source says");
        return;
    }
    if (parent.has_value() && isUnder(entities_, parents_, entities_[*parent], kEntity)) {
        refuse(kName + " cannot follow what follows it");
        return;
    }
    // The game's attachment, by the catalog's name for it.
    std::optional<Catalog::Component> attachment;
    for (const std::string_view kAttachment : kAttachments) {
        std::string why;
        if (const auto kFound = componentNamed(catalog_, kAttachment, why);
            kFound.has_value() && kFound->name == kAttachment) {
            attachment = kFound;
            break;
        }
    }
    if (!attachment.has_value()) {
        refuse("an entity follows another by an attachment, which a game with physics has");
        return;
    }
    const std::optional<Value> kRead = read(scene_, "scene.read_entity", kEntity);
    const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
    const auto kHas = [&](const std::string& id) {
        for (const Value& each : components != nullptr && components->kind() == Value::Kind::Array
                                     ? components->items()
                                     : std::span<const Value>{}) {
            const Value* component = each.find("component");
            if (component != nullptr && component->text() != nullptr && *component->text() == id) {
                return true;
            }
        }
        return false;
    };
    const bool kAttached = kHas(attachment->id);
    std::vector<Value> operations;
    if (!parent.has_value()) {
        if (!kAttached) {
            say(kName + " follows nothing already");
            return;
        }
        Value removed = Value::object();
        removed.add("operation", Value::string("scene.remove_component"));
        removed.add("entity", Value::string(kEntity));
        removed.add("component", Value::string(attachment->id));
        operations.push_back(std::move(removed));
        commitAll(std::move(operations), kName + " follows nothing", kEntity);
        return;
    }
    if (!kAttached) {
        Value added = Value::object();
        added.add("operation", Value::string("scene.add_component"));
        added.add("entity", Value::string(kEntity));
        added.add("component", Value::string(attachment->id));
        operations.push_back(std::move(added));
    }
    Value set = Value::object();
    set.add("operation", Value::string("scene.set_reference"));
    set.add("entity", Value::string(kEntity));
    set.add("component", Value::string(attachment->id));
    set.add("field", Value::string("parent"));
    set.add("target", Value::string(entities_[*parent]));
    operations.push_back(std::move(set));
    const std::string kParentName = names_[*parent].empty() ? entities_[*parent].substr(0, 8) : names_[*parent];
    commitAll(std::move(operations), kName + " follows " + kParentName, kEntity);
}

} // namespace rawframe::studio
