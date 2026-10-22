#include "hints.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

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
        say(std::to_string(selected_.size()) + " entities chosen");
    } else {
        const auto kOne = std::ranges::find(entities_, selected_.front());
        showEntity(kOne != entities_.end() ? static_cast<std::size_t>(kOne - entities_.begin()) : at);
    }
}

bool ShellParticipant::chooseBoxed(const std::array<std::array<double, 3>, 2>& box) {
    using Point = std::array<double, 3>;
    const auto kPointOf = [this](std::string_view name) -> std::optional<Point> {
        const Value* kPoint = view_.has_value() ? view_->find(name) : nullptr;
        if (kPoint == nullptr || kPoint->items().size() != 3) {
            return std::nullopt;
        }
        return Point{kPoint->items()[0].real().value_or(0),
                     kPoint->items()[1].real().value_or(0),
                     kPoint->items()[2].real().value_or(0)};
    };
    const std::optional<Point> kEye = kPointOf("eye");
    const std::optional<Point> kTarget = kPointOf("target");
    if (!kEye.has_value() || !kTarget.has_value()) {
        return false;
    }
    const auto kDot = [](const Point& a, const Point& b) {
        return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
    };
    const auto kCross = [](const Point& a, const Point& b) {
        return Point{(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
    };
    const auto kUnit = [&kDot](const Point& a) {
        const double kLength = std::sqrt(kDot(a, a));
        return kLength > 1e-9 ? Point{a[0] / kLength, a[1] / kLength, a[2] / kLength} : Point{};
    };
    // The view's own axes, and each way across it as the view's picture
    // places it: across and up over ahead, as a lens draws it.
    const Point kAhead = kUnit({(*kTarget)[0] - (*kEye)[0], (*kTarget)[1] - (*kEye)[1], (*kTarget)[2] - (*kEye)[2]});
    Point across = kUnit(kCross(kAhead, {0, 1, 0}));
    if (kDot(across, across) == 0) {
        across = {1, 0, 0};
    }
    const Point kUp = kCross(across, kAhead);
    const auto kPlaced = [&](const Point& way) -> std::optional<std::array<double, 2>> {
        const double kForward = kDot(way, kAhead);
        if (kForward <= 1e-6) {
            return std::nullopt;
        }
        return std::array<double, 2>{kDot(way, across) / kForward, kDot(way, kUp) / kForward};
    };
    const auto kFrom = kPlaced(box[0]);
    const auto kTo = kPlaced(box[1]);
    // A press let go about where it went down, a hundredth of the view's
    // reach across, is a click.
    if (!kFrom.has_value() || !kTo.has_value() ||
        std::max(std::abs((*kFrom)[0] - (*kTo)[0]), std::abs((*kFrom)[1] - (*kTo)[1])) < 0.01) {
        return false;
    }
    std::vector<std::string> boxed;
    std::optional<std::size_t> first;
    for (std::size_t at = 0; at < entities_.size(); ++at) {
        if (lockedOf(entities_[at]).has_value()) {
            continue;
        }
        const auto kPose = poseOf(entities_[at]);
        if (!kPose.has_value()) {
            continue;
        }
        const auto kSeen =
            kPlaced({kPose->at("x") - (*kEye)[0], kPose->at("y") - (*kEye)[1], kPose->at("z") - (*kEye)[2]});
        if (!kSeen.has_value()) {
            continue;
        }
        bool inside = true;
        for (std::size_t side = 0; side < 2; ++side) {
            inside = inside && (*kSeen)[side] >= std::min((*kFrom)[side], (*kTo)[side]) &&
                     (*kSeen)[side] <= std::max((*kFrom)[side], (*kTo)[side]);
        }
        if (inside) {
            boxed.push_back(entities_[at]);
            first = first.value_or(at);
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kBoxed,
                 "a box drawn across the view chose what stands in it",
                 {diagnostics::field("chosen", static_cast<std::uint64_t>(boxed.size()))});
    if (!first.has_value()) {
        say("nothing in the box");
        return true;
    }
    selected_ = std::move(boxed);
    anchor_ = first;
    if (selected_.size() > 1) {
        showSelection(*first);
        say(std::to_string(selected_.size()) + " entities chosen");
    } else {
        showEntity(*first);
    }
    return true;
}

void ShellParticipant::showSelection(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    entity_ = entities_[at];
    // All of them chosen in the session too, so framing holds them all
    // (D792).
    static_cast<void>(ask(selectRecord(next(), scene_, selected_)));
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

void ShellParticipant::refresh(const std::optional<std::string>& entity) {
    const std::array<float, 2> kEntities = tree_->scrollOf(entitiesColumn_);
    const std::array<float, 2> kComponents = tree_->scrollOf(componentsColumn_);
    const std::vector<std::string> kChosenBefore = selected_;
    const bool kShownBefore = !entity_.empty();
    showScene(sceneAt_);
    if (entity.has_value()) {
        if (const auto kAt = std::ranges::find(entities_, *entity); kAt != entities_.end()) {
            showEntity(static_cast<std::size_t>(kAt - entities_.begin()));
            // Several chosen stay chosen while they are all still there.
            if (kChosenBefore.size() > 1 && std::ranges::all_of(kChosenBefore, [&](const std::string& each) {
                    return std::ranges::contains(entities_, each);
                })) {
                selected_ = kChosenBefore;
                showSelection(static_cast<std::size_t>(kAt - entities_.begin()));
            }
        }
    }
    // What an undo, or another's edit, took away is chosen no longer: its
    // outline and its mark go with it (D997).
    std::erase_if(selected_, [this](const std::string& each) {
        return !std::ranges::contains(entities_, each);
    });
    if (entity_.empty() && (kShownBefore || !kChosenBefore.empty())) {
        selected_.clear();
        static_cast<void>(ask(selectRecord(next(), scene_, selected_)));
        markChosen();
    }
    static_cast<void>(tree_->scrollTo(entitiesColumn_, kEntities[0], kEntities[1]));
    static_cast<void>(tree_->scrollTo(componentsColumn_, kComponents[0], kComponents[1]));
    showHistoryList();
}

void ShellParticipant::stepTo(std::size_t entry) {
    const auto kTarget = static_cast<std::int64_t>(entry) + 1;
    // Each step is one record, as Undo and Redo are; a step the session
    // refuses leaves the counts as they were and ends the walk.
    while (undoable_ != kTarget) {
        const std::int64_t kBefore = undoable_;
        step(undoable_ > kTarget ? "authoring.undo" : "authoring.redo");
        if (undoable_ == kBefore) {
            break;
        }
    }
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
        recookPlayed();
    } else {
        say(kOutcome.message);
    }
    // A scene's entities chosen as the step left them, as an editor's undo
    // puts back what was chosen with what was done (D417, D997); another
    // document chooses none of its own.
    if (kOutcome.done && kOutcome.selection.has_value() && chosen(Listed::Scene)) {
        selected_ = *kOutcome.selection;
        refresh(selected_.empty() ? std::nullopt : std::optional{selected_.front()});
        return;
    }
    refresh(entity_.empty() ? std::nullopt : std::optional{entity_});
}

} // namespace rawframe::studio
