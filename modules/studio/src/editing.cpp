#include "shell.h"

namespace rawframe::studio {

std::string ShellParticipant::ask(const Value& record) {
    bool ended = false;
    ++records_;
    return session_->answer(document::writeCompact(record), ended);
}

std::int64_t ShellParticipant::next() const noexcept {
    return static_cast<std::int64_t>(records_ + 1);
}

std::optional<Value>
ShellParticipant::read(const std::string& scene, std::string_view operation, std::string_view entity) {
    return firstAnswer(ask(readRecord(next(), scene, operation, entity)));
}

void ShellParticipant::pressAt(float x, float y) {
    const auto kHit = tree_->hit(root_, x, y);
    if (!kHit.has_value() || !kHit->node.has_value()) {
        return;
    }
    // The row is the node hit or the one its words are on.
    const ui::Node kNode = *kHit->node;
    if (play_.has_value() && kNode == playNode_) {
        endEdit();
        if (playing_.has_value()) {
            stopPlaying();
        } else {
            startPlaying();
        }
    } else if (kNode == undoNode_ || kNode == redoNode_) {
        endEdit();
        step(kNode == undoNode_ ? "authoring.undo" : "authoring.redo");
    } else if (kNode == newNode_ && !scene_.empty()) {
        endEdit();
        create();
    } else if (kNode == deleteNode_ && !entity_.empty()) {
        endEdit();
        // An entity an instance brought is removed through its patch, so
        // Restore takes it back (D154, D450).
        const bool kBrought = entityAt_ < brought_.size() && brought_[entityAt_];
        commit(operationOn("scene.destroy_entity"),
               kBrought ? "removed from its instance" : "entity deleted",
               kBrought ? std::optional<std::string>{entity_} : std::nullopt);
    } else if ((kNode == upNode_ || kNode == downNode_) && !entity_.empty()) {
        endEdit();
        move(kNode == upNode_ ? -1 : 1);
    } else if ((kNode == restoreNode_ || kNode == uninstanceNode_) && !entity_.empty()) {
        endEdit();
        const bool kBrought = entityAt_ < brought_.size() && brought_[entityAt_];
        const bool kRemoved = entityAt_ < removed_.size() && removed_[entityAt_];
        if (!kBrought || (kNode == restoreNode_ && !kRemoved)) {
            ++refused_;
            say(kBrought ? "its instance has not removed it" : "no instance brought it");
        } else if (kNode == restoreNode_) {
            commit(operationOn("scene.restore_entity"), "restored", entity_);
        } else {
            commit(operationOn("scene.remove_instance"), "instance removed", std::nullopt);
        }
    } else if (const auto kAction = std::ranges::find(actions_, kNode, &ActionButton::node);
               kAction != actions_.end()) {
        endEdit();
        const ActionButton kAsked = *kAction;
        Value operation = operationOn(kAsked.operation);
        operation.add("component", Value::string(kAsked.component));
        if (!kAsked.field.empty()) {
            operation.add("field", Value::string(kAsked.field));
        }
        commit(std::move(operation), kAsked.done, entity_);
    } else if (const auto kScene = std::ranges::find(sceneRows_, kNode); kScene != sceneRows_.end()) {
        showScene(static_cast<std::size_t>(kScene - sceneRows_.begin()));
    } else if (const auto kEntity = std::ranges::find(entityRows_, kNode); kEntity != entityRows_.end()) {
        showEntity(static_cast<std::size_t>(kEntity - entityRows_.begin()));
    } else if (const auto kFieldAt = std::ranges::find(fields_, kNode, &FieldRow::value); kFieldAt != fields_.end()) {
        beginEdit(*kFieldAt);
    } else if (const auto kViewAt = std::ranges::find(viewFields_, kNode, &FieldRow::value);
               kViewAt != viewFields_.end()) {
        beginEdit(*kViewAt);
    } else if (newScene_.has_value() && kNode == newScene_->value) {
        beginEdit(*newScene_);
    } else if (const auto kSceneAt = std::ranges::find(sceneFields_, kNode, &FieldRow::value);
               kSceneAt != sceneFields_.end()) {
        beginEdit(*kSceneAt);
    } else {
        endEdit();
    }
}

void ShellParticipant::beginEdit(const FieldRow& row) {
    endEdit();
    editing_ = row;
    edit_ = std::make_unique<ui::TextEdit>(
        *tree_, row.value, ui::EditSettings{.caretColor = kText, .selectionColor = 0x4A6FA5AAU});
    static_cast<void>(edit_->press(ui::EditKey::SelectAll, {}));
    static_cast<void>(tree_->setLook(row.value, ui::Look{.fill = kEditing, .radius = 3}));
    if (typing_ != nullptr) {
        const auto kPlace = tree_->placeOf(root_, row.value);
        typing_->focus(kPlace.has_value() ? view::UiTyping::Caret{kPlace->x, kPlace->y, kPlace->width, kPlace->height}
                                          : view::UiTyping::Caret{});
    }
}

void ShellParticipant::endEdit() {
    if (edit_ == nullptr) {
        return;
    }
    edit_.reset();
    if (editing_.has_value() && tree_->contains(editing_->value)) {
        static_cast<void>(tree_->setLook(editing_->value, ui::Look{.fill = kField, .radius = 3}));
    }
    editing_.reset();
    if (typing_ != nullptr) {
        typing_->focus(std::nullopt);
    }
}

void ShellParticipant::take(const view::Typing& typing) {
    if (edit_ == nullptr || !editing_.has_value()) {
        return;
    }
    const std::optional<view::TypingKey> kLeft = view::edit(*edit_, typing);
    if (kLeft == view::TypingKey::Submit) {
        const FieldRow kEdited = *editing_;
        const std::string kTyped{tree_->textOf(kEdited.value)};
        endEdit();
        if (kEdited.role == FieldRow::Role::View) {
            setView(kEdited.field, kTyped);
        } else if (kEdited.role == FieldRow::Role::Instance) {
            place(kTyped);
        } else if (kEdited.role == FieldRow::Role::NewScene) {
            makeScene(kTyped);
        } else {
            apply(kEdited, kTyped);
        }
    } else if (kLeft == view::TypingKey::Dismiss) {
        const FieldRow::Role kRole = editing_->role;
        endEdit();
        if (kRole == FieldRow::Role::View) {
            showViewText();
        } else if (kRole == FieldRow::Role::NewScene) {
            static_cast<void>(showScenes());
        } else if (kRole == FieldRow::Role::Instance) {
            refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
        } else {
            showEntity(entityAt_);
        }
    }
}

void ShellParticipant::setView(const std::string& part, const std::string& text) {
    const std::optional<Value> kView = viewWith(view_, part, text);
    if (!kView.has_value()) {
        ++refused_;
        say(part + ": " + (part == "fieldOfView" ? "one number" : "three numbers"));
        showViewText();
        return;
    }
    const Answered kAnswered = answeredOf(ask(viewRecord(next(), scene_, *kView)));
    if (kAnswered.done) {
        ++viewsSet_;
        view_ = kAnswered.view;
        previewing_ = kAnswered.previewing;
        say(previewing_ ? "view set and previewed" : "view set");
    } else {
        ++refused_;
        say(kAnswered.message);
    }
    showViewText();
}

Value ShellParticipant::operationOn(std::string_view operation) const {
    Value made = Value::object();
    made.add("operation", Value::string(std::string{operation}));
    made.add("entity", Value::string(entity_));
    return made;
}

void ShellParticipant::apply(const FieldRow& row, const std::string& text) {
    Value operation;
    std::string done;
    if (row.role == FieldRow::Role::Name) {
        operation = operationOn("scene.rename_entity");
        operation.add("name", Value::string(text));
        done = "renamed " + text;
    } else if (row.role == FieldRow::Role::Add) {
        std::string why;
        const std::optional<Catalog::Component> kAdded = componentNamed(catalog_, text, why);
        if (!kAdded.has_value()) {
            refuse(why);
            return;
        }
        operation = operationOn("scene.add_component");
        operation.add("component", Value::string(kAdded->id));
        done = kAdded->name + " added";
    } else if (row.kind == "reference" || row.kind == "entity") {
        // A reference, unset (its type's kind) or set (its value's), names
        // an entity of the scene, or none when cleared.
        operation = operationOn("scene.set_reference");
        operation.add("component", Value::string(row.component));
        operation.add("field", Value::string(row.field));
        if (text.empty()) {
            operation.add("target", Value{});
            done = row.field + " cleared";
        } else {
            std::string why;
            const std::optional<std::string> kTarget = entityNamed(entities_, names_, text, why);
            if (!kTarget.has_value()) {
                refuse(row.field + ": " + why);
                return;
            }
            operation.add("target", Value::string(*kTarget));
            done = row.field + " set to " + text;
        }
    } else {
        const std::optional<Value> kValue = typedValue(row.kind, text);
        if (!kValue.has_value()) {
            refuse(row.field + ": " + (row.kind.empty() ? std::string{"not edited here"} : "not a " + row.kind));
            return;
        }
        operation = operationOn("scene.set_field");
        operation.add("component", Value::string(row.component));
        operation.add("field", Value::string(row.field));
        operation.add("value", *kValue);
        done = row.field + " set to " + text;
    }
    commit(std::move(operation), done, entity_);
}

void ShellParticipant::create() {
    const std::string kIdentity = mintedIdentity();
    Value operation = Value::object();
    operation.add("operation", Value::string("scene.create_entity"));
    operation.add("entity", Value::string(kIdentity));
    operation.add("name", Value::string("new entity"));
    commit(std::move(operation), "entity created", kIdentity);
}

void ShellParticipant::move(int by) {
    const std::optional<std::int64_t> kPlace = entityAt_ < places_.size() ? places_[entityAt_] : std::nullopt;
    const auto kOwn = std::ranges::count_if(places_, [](const auto& place) {
        return place.has_value();
    });
    if (!kPlace.has_value() || *kPlace + by < 0 || *kPlace + by >= kOwn) {
        ++refused_;
        say(!kPlace.has_value() ? "an instance places what it brings" : by < 0 ? "already first" : "already last");
        return;
    }
    Value operation = operationOn("scene.move_entity");
    operation.add("place", Value::integer(*kPlace + by));
    commit(std::move(operation), by < 0 ? "moved up" : "moved down", entity_);
}

void ShellParticipant::place(const std::string& text) {
    std::string why;
    const std::optional<std::size_t> kScene = sceneNamed(scenes_, text, why);
    if (!kScene.has_value()) {
        ++refused_;
        say(why);
        refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
        return;
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("scene.add_instance"));
    operation.add("scene", Value::string(sceneSources_[*kScene]));
    operation.add("instance", Value::string(mintedIdentity()));
    commit(std::move(operation), "instance of " + scenes_[*kScene] + " placed", std::nullopt);
}

void ShellParticipant::makeScene(std::string text) {
    if (!text.empty() && !text.ends_with(".scene")) {
        text += ".scene";
    }
    const Answered kMade = answeredOf(ask(createSceneRecord(next(), text)));
    if (!kMade.done || text.empty()) {
        ++refused_;
        say(kMade.done ? "name the new scene" : kMade.message);
        static_cast<void>(showScenes());
        return;
    }
    ++applied_;
    const auto kAt = std::ranges::upper_bound(scenes_, text);
    const auto kIndex = static_cast<std::size_t>(kAt - scenes_.begin());
    scenes_.insert(kAt, text);
    sceneSources_.insert(sceneSources_.begin() + static_cast<std::ptrdiff_t>(kIndex), kMade.resource);
    // The chosen scene's row moves down when the new one sorts before it.
    if (!scene_.empty() && kIndex <= sceneAt_) {
        ++sceneAt_;
    }
    static_cast<void>(showScenes());
    showScene(kIndex);
    say(text + " made");
}

void ShellParticipant::refuse(const std::string& why) {
    ++refused_;
    say(why);
    showEntity(entityAt_);
}

void ShellParticipant::commit(Value operation, const std::string& done, const std::optional<std::string>& choose) {
    const Outcome kOutcome = outcomeOf(ask(applyRecord(next(), scene_, std::move(operation))));
    told(kOutcome);
    if (kOutcome.done) {
        ++applied_;
        say(done);
    } else {
        ++refused_;
        say(kOutcome.message);
    }
    refresh(choose);
}

void ShellParticipant::refresh(const std::optional<std::string>& entity) {
    const std::array<float, 2> kEntities = tree_->scrollOf(entitiesColumn_);
    const std::array<float, 2> kComponents = tree_->scrollOf(componentsColumn_);
    showScene(sceneAt_);
    if (entity.has_value()) {
        if (const auto kAt = std::ranges::find(entities_, *entity); kAt != entities_.end()) {
            showEntity(static_cast<std::size_t>(kAt - entities_.begin()));
        }
    }
    static_cast<void>(tree_->scrollTo(entitiesColumn_, kEntities[0], kEntities[1]));
    static_cast<void>(tree_->scrollTo(componentsColumn_, kComponents[0], kComponents[1]));
}

void ShellParticipant::step(std::string_view kind) {
    if (scene_.empty()) {
        return;
    }
    const Outcome kOutcome = outcomeOf(ask(stepRecord(next(), kind, scene_)));
    told(kOutcome);
    const bool kUndo = kind == "authoring.undo";
    if (kOutcome.done) {
        ++(kUndo ? undone_ : redone_);
        say(kUndo ? "undone" : "redone");
    } else {
        say(kOutcome.message);
    }
    refresh(entity_.empty() ? std::nullopt : std::optional{entity_});
}

void ShellParticipant::told(const Outcome& outcome) {
    undoable_ = outcome.undoable;
    redoable_ = outcome.redoable;
    static_cast<void>(showHistory());
    if (outcome.view.has_value()) {
        view_ = outcome.view;
        showViewText();
    }
}

} // namespace rawframe::studio
