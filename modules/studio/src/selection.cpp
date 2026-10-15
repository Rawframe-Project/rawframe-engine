#include "hints.h"
#include "shell.h"

#include <algorithm>

namespace rawframe::studio {

namespace {

/// `operation` as it is, but done to `entity`.
Value doneTo(const Value& operation, const std::string& entity) {
    Value made = Value::object();
    for (std::size_t at = 0; at < operation.names().size(); ++at) {
        made.add(operation.names()[at],
                 operation.names()[at] == "entity" ? Value::string(entity) : operation.items()[at]);
    }
    return made;
}

} // namespace

void ShellParticipant::chooseEntity(std::size_t at) {
    // Only a pointer's press holds keys; a screen reader's chooses one.
    const view::UiPointing::Held kHeld =
        pointerPress_ && pointing_ != nullptr ? pointing_->held() : view::UiPointing::Held{};
    const std::string& kEntity = entities_[at];
    if (kHeld.control) {
        // Added, or taken away; never left with none.
        if (const auto kThere = std::ranges::find(selected_, kEntity); kThere != selected_.end()) {
            if (selected_.size() > 1) {
                selected_.erase(kThere);
            }
        } else {
            selected_.push_back(kEntity);
        }
        anchor_ = at;
    } else if (kHeld.shift && anchor_.has_value() && *anchor_ < entities_.size()) {
        // From the last one chosen to this one, as the Hierarchy lists them.
        selected_.clear();
        for (std::size_t each = std::min(*anchor_, at); each <= std::max(*anchor_, at); ++each) {
            selected_.push_back(entities_[each]);
        }
    } else {
        selected_ = {kEntity};
        anchor_ = at;
    }
    if (selected_.size() > 1) {
        showSelection(at);
    } else {
        const auto kOne = std::ranges::find(entities_, selected_.front());
        showEntity(kOne != entities_.end() ? static_cast<std::size_t>(kOne - entities_.begin()) : at);
    }
}

void ShellParticipant::showSelection(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        const bool kIn = std::ranges::contains(selected_, entities_[each]);
        static_cast<void>(tree_->setLook(entityRows_[each], ui::Look{.fill = kIn ? kChosen : kRow, .radius = 4}));
    }
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    // Each one read, then what they all have: the components they share,
    // each field's value where they agree and empty where they differ
    // (SPEC-0051's one selection model).
    std::vector<std::optional<Value>> reads;
    for (const std::string& kEach : selected_) {
        reads.push_back(read(scene_, "scene.read_entity", kEach));
    }
    const auto kComponentsOf = [](const std::optional<Value>& reading) {
        const Value* components = reading.has_value() ? reading->find("components") : nullptr;
        return components != nullptr && components->kind() == Value::Kind::Array ? components->items()
                                                                                 : std::span<const Value>{};
    };
    const auto kOf = [&](const std::optional<Value>& reading, const std::string& id) -> const Value* {
        for (const Value& each : kComponentsOf(reading)) {
            const Value* component = each.find("component");
            if (component != nullptr && component->text() != nullptr && *component->text() == id) {
                return &each;
            }
        }
        return nullptr;
    };
    if (auto heading = row(componentsColumn_, std::to_string(selected_.size()) + " entities chosen", kQuiet, kPanel);
        heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    for (const Value& first : kComponentsOf(reads.front())) {
        const Value* component = first.find("component");
        const Value* name = first.find("name");
        if (component == nullptr || component->text() == nullptr) {
            continue;
        }
        const std::string kComponent = *component->text();
        if (!std::ranges::all_of(reads, [&](const std::optional<Value>& each) {
                return kOf(each, kComponent) != nullptr;
            })) {
            continue;
        }
        ++components_;
        if (auto heading = row(componentsColumn_,
                               name != nullptr && name->text() != nullptr ? *name->text() : "?",
                               kText,
                               kHeaderFill);
            heading.has_value()) {
            componentRows_.push_back(*heading);
        }
        const Catalog::Component* type = catalog_.component(kComponent);
        for (const FieldShown& kFirstField : fieldsShown(type, first.find("fields"))) {
            // The value they all have, or none where any differs.
            std::optional<std::string> shared = kFirstField.text;
            for (std::size_t each = 1; each < reads.size() && shared.has_value(); ++each) {
                for (const FieldShown& kOther : fieldsShown(type, kOf(reads[each], kComponent)->find("fields"))) {
                    if (kOther.name == kFirstField.name && kOther.text != shared) {
                        shared.reset();
                    }
                }
            }
            const Catalog::Field kType =
                kFirstField.type.value_or(Catalog::Field{.name = kFirstField.name, .kind = kFirstField.kind});
            const std::string kValueText = shared.has_value() && (kType.color || !kType.labels.empty())
                                               ? shownAs(kType, *shared)
                                               : shared.value_or(std::string{});
            auto line = fieldRow(kFirstField.name, kValueText, std::nullopt, shared.has_value());
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            fields_.push_back(FieldRow{.value = line->second,
                                       .component = kComponent,
                                       .field = kFirstField.name,
                                       .kind = kFirstField.kind,
                                       .type = kType});
        }
    }
}

void ShellParticipant::commitChosen(Value operation, const std::string& done) {
    if (selected_.size() <= 1) {
        commit(std::move(operation), done, entity_);
        return;
    }
    // One step for all of them, so one Undo takes it back.
    std::vector<Value> operations;
    for (const std::string& kEach : selected_) {
        operations.push_back(doneTo(operation, kEach));
    }
    ++together_;
    commitAll(std::move(operations), done + " on " + std::to_string(selected_.size()), entity_);
}

} // namespace rawframe::studio
