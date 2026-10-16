#include "hints.h"
#include "shell.h"

namespace rawframe::studio {

namespace {

/// The member an array's item is known by, if it has one: the first whose
/// name ends in `Id` and holds text (an action's `actionId`, a bus's
/// `busId`). An array whose items have none is a value, edited whole.
std::optional<std::string> keyOf(const Value& item) {
    if (item.kind() != Value::Kind::Object) {
        return std::nullopt;
    }
    for (std::size_t each = 0; each < item.names().size(); ++each) {
        if (item.names()[each].ends_with("Id") && item.items()[each].kind() == Value::Kind::String) {
            return item.names()[each];
        }
    }
    return std::nullopt;
}

/// Whether every item of `array` is known by a key of its own.
bool keyed(const Value& array) {
    return !array.items().empty() && std::ranges::all_of(array.items(), [](const Value& item) {
        return keyOf(item).has_value();
    });
}

/// How a row names an item: its name where it has one, else its key,
/// followed by the array it is in, as `aim (actions)`.
std::string labelOf(const Value& item, const std::string& key) {
    for (const std::string_view kNamed : {"name", "displayName"}) {
        if (const Value* kName = item.find(kNamed); kName != nullptr && kName->kind() == Value::Kind::String) {
            return *kName->text();
        }
    }
    return *item.find(key)->text();
}

std::string joined(const std::string& path, const std::string& step) {
    return path.empty() ? step : path + "." + step;
}

} // namespace

void ShellParticipant::showData() {
    const std::optional<Value> kRead = read(scene_, "data.read");
    sheet_ =
        kRead.has_value() && kRead->find("data") != nullptr ? std::optional<Value>{*kRead->find("data")} : std::nullopt;
    held_.data.clear();
    if (!sheet_.has_value()) {
        return;
    }
    // The document first, then each object and keyed item in it before
    // what it holds, as the document nests them.
    const auto kList = [this](const auto& self,
                              const Value& at,
                              const std::string& path,
                              const std::string& label,
                              std::size_t depth) -> void {
        auto added = row(entitiesColumn_, std::string(2 * depth, ' ') + label, kText, kRow, true);
        if (!added.has_value() || entityRows_.size() >= EditorsHeld::kMostRows) {
            return;
        }
        entityRows_.push_back(*added);
        entities_.push_back(path);
        names_.push_back(label);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
        held_.data.push_back(at);
        for (std::size_t each = 0; each < at.names().size(); ++each) {
            const std::string& kMember = at.names()[each];
            const Value& kHeld = at.items()[each];
            if (kHeld.kind() == Value::Kind::Object) {
                self(self, kHeld, joined(path, kMember), kMember, depth + 1);
            } else if (kHeld.kind() == Value::Kind::Array && keyed(kHeld)) {
                for (const Value& kItem : kHeld.items()) {
                    const std::string kKey = *keyOf(kItem);
                    self(self,
                         kItem,
                         joined(path, kMember + "[" + kKey + "=" + *kItem.find(kKey)->text() + "]"),
                         labelOf(kItem, kKey) + " (" + kMember + ")",
                         depth + 1);
                }
            }
        }
    };
    kList(kList, *sheet_, "", std::filesystem::path{scene_}.filename().string(), 0);
    // An animation graph's nodes on the canvas too (D819).
    const Value* kKind = sheet_->find("kind");
    const Value* kGraph = sheet_->find("graph");
    if (kKind != nullptr && kKind->kind() == Value::Kind::String && *kKind->text() == "animation.graph" &&
        kGraph != nullptr) {
        showCanvas(*kGraph, "graph.");
    }
}

void ShellParticipant::showDataEntry(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    if (entity_ != entities_[at]) {
        static_cast<void>(tree_->scrollTo(componentsColumn_, 0, 0));
    }
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    markCanvas();
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    if (at >= held_.data.size()) {
        return;
    }
    const Value& kHeld = held_.data[at];
    auto heading = row(componentsColumn_, entity_.empty() ? names_[at] : entity_, kQuiet, kPanel);
    if (heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    // Each value it holds a field: text as it reads, anything else as JSON;
    // what is a row of the Hierarchy is not repeated here.
    for (std::size_t each = 0; each < kHeld.names().size(); ++each) {
        const std::string& kMember = kHeld.names()[each];
        const Value& kValue = kHeld.items()[each];
        if (kValue.kind() == Value::Kind::Object || (kValue.kind() == Value::Kind::Array && keyed(kValue))) {
            continue;
        }
        const bool kTextual = kValue.kind() == Value::Kind::String;
        auto line = fieldRow(kMember, kTextual ? *kValue.text() : document::writeCompact(kValue));
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(FieldRow{.value = line->second,
                                   .role = FieldRow::Role::DataValue,
                                   .field = joined(entity_, kMember),
                                   .kind = kTextual ? "string" : "json"});
    }
    ++components_;
}

void ShellParticipant::setDataValue(const FieldRow& row, const std::string& text) {
    // Text as text, where it held text; else the JSON typed, or text where
    // it does not read as JSON; nothing takes the value away.
    Value value = text.empty() ? Value{} : row.kind == "string" ? Value::string(text) : documentedValue(text);
    Value operation = Value::object();
    operation.add("operation", Value::string("data.set"));
    operation.add("path", Value::string(row.field));
    operation.add("value", std::move(value));
    commit(std::move(operation), row.field + (text.empty() ? " removed" : " set to " + text), entity_);
}

} // namespace rawframe::studio
