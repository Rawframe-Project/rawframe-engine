// Entities made again from others (D458, D999): the chosen one duplicated
// beside itself, and the chosen ones copied, then pasted as new entities
// into whichever scene is open, each with its components and the values
// the scene gives them.

#include "shell.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::studio {

void ShellParticipant::duplicate() {
    const bool kBrought = entityAt_ < brought_.size() && brought_[entityAt_];
    const std::optional<Value> kRead = read(scene_, "scene.read_entity", entity_);
    const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
    if (kBrought || components == nullptr || components->kind() != Value::Kind::Array) {
        ++refused_;
        say(kBrought ? "an instance's entity is its source's to copy" : "nothing to copy");
        return;
    }
    const std::string kCopy = mintedIdentity();
    const std::string kName = (entityAt_ < names_.size() ? names_[entityAt_] : std::string{}) + " copy";
    commitAll(copyOf(*components, kCopy, kName), kName + " made", kCopy);
}

void ShellParticipant::copyOrPaste(bool paste) {
    if (!paste) {
        // Each chosen entity as the scene reads it now, its name kept; an
        // instance's is its source's.
        std::vector<std::pair<std::string, Value>> copied;
        for (const std::string& kEach : selected_.empty() ? std::vector<std::string>{entity_} : selected_) {
            const auto kAt = std::ranges::find(entities_, kEach);
            const std::size_t kIndex = static_cast<std::size_t>(kAt - entities_.begin());
            if (kEach.empty() || kAt == entities_.end() || (kIndex < brought_.size() && brought_[kIndex])) {
                continue;
            }
            const std::optional<Value> kRead = read(scene_, "scene.read_entity", kEach);
            const Value* kComponents = kRead.has_value() ? kRead->find("components") : nullptr;
            if (kComponents != nullptr && kComponents->kind() == Value::Kind::Array) {
                copied.emplace_back(kIndex < names_.size() ? names_[kIndex] : std::string{}, *kComponents);
            }
        }
        if (copied.empty()) {
            ++refused_;
            say("choose an entity of the scene's own to copy it");
            return;
        }
        held_.copied = std::move(copied);
        say(std::to_string(held_.copied.size()) + (held_.copied.size() == 1 ? " entity" : " entities") + " copied");
        return;
    }
    if (held_.copied.empty() || scene_.empty()) {
        ++refused_;
        say("nothing copied to paste");
        return;
    }
    // Each a new entity of the scene open, in one transaction, the first
    // chosen.
    std::vector<Value> operations;
    std::string first;
    for (const auto& [kName, kComponents] : held_.copied) {
        const std::string kMade = mintedIdentity();
        first = first.empty() ? kMade : first;
        for (Value& each : copyOf(kComponents, kMade, kName)) {
            operations.push_back(std::move(each));
        }
    }
    const std::size_t kCount = held_.copied.size();
    commitAll(
        std::move(operations), std::to_string(kCount) + (kCount == 1 ? " entity" : " entities") + " pasted", first);
}

std::vector<Value>
ShellParticipant::copyOf(const Value& components, const std::string& entity, const std::string& called) {
    std::vector<Value> operations;
    Value made = Value::object();
    made.add("operation", Value::string("scene.create_entity"));
    made.add("entity", Value::string(entity));
    made.add("name", Value::string(called));
    operations.push_back(std::move(made));
    for (const Value& each : components.items()) {
        const Value* component = each.find("component");
        if (component == nullptr || component->text() == nullptr) {
            continue;
        }
        Value added = Value::object();
        added.add("operation", Value::string("scene.add_component"));
        added.add("entity", Value::string(entity));
        added.add("component", *component);
        operations.push_back(std::move(added));
        // Each value the scene gives, as it reads: a reference names its
        // target again, and every other field takes its value. A value only
        // recorded, its field no longer the catalog's, is left behind, as a
        // component the catalog no longer knows is above.
        const Value* fields = each.find("fields");
        for (const Value& field :
             fields != nullptr && fields->kind() == Value::Kind::Array ? fields->items() : std::span<const Value>{}) {
            const Value* name = field.find("name");
            const Value* value = field.find("value");
            if (name == nullptr || name->text() == nullptr || value == nullptr ||
                value->kind() != Value::Kind::Object || value->names().size() != 1) {
                continue;
            }
            const std::string& kKind = value->names().front();
            if (kKind == "recorded") {
                continue;
            }
            Value set = Value::object();
            set.add("entity", Value::string(entity));
            set.add("component", *component);
            set.add("field", *name);
            if (kKind == "entity") {
                set.add("operation", Value::string("scene.set_reference"));
                set.add("target", *value->find("entity"));
            } else {
                set.add("operation", Value::string("scene.set_field"));
                set.add("value", *value);
            }
            operations.push_back(std::move(set));
        }
    }
    return operations;
}

} // namespace rawframe::studio
