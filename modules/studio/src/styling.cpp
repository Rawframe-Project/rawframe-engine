// A game's style sheet in Studio (D637): listed with the scenes, its
// classes in the entity column, made with New, deleted with Delete, and
// named in the name field (D638), and a chosen class's looks as fields,
// each part set through the session's `styles.set_part` (D636), which a
// played game follows (D634).

#include "rawframe/ui/styles.h"
#include "shell.h"

namespace rawframe::studio {

namespace {

/// The class `id` names in `sheet`, if it has one.
const Value* classOf(const std::optional<Value>& sheet, const std::string& id) {
    const Value* styles = sheet.has_value() ? sheet->find("styles") : nullptr;
    for (const Value& each : styles != nullptr ? styles->items() : std::span<const Value>{}) {
        const Value* held = each.find("styleId");
        if (held != nullptr && held->text() != nullptr && *held->text() == id) {
            return &each;
        }
    }
    return nullptr;
}

/// A part's value as a field shows it: a color or other text as it is,
/// anything else as JSON.
std::string partText(const Value& value) {
    return value.text() != nullptr ? *value.text() : document::writeCompact(value);
}

} // namespace

void ShellParticipant::showSheet() {
    const std::optional<Value> kRead = read(scene_, "styles.read_sheet");
    sheet_ = kRead.has_value() && kRead->find("sheet") != nullptr ? std::optional<Value>{*kRead->find("sheet")}
                                                                  : std::nullopt;
    const Value* styles = sheet_.has_value() ? sheet_->find("styles") : nullptr;
    for (const Value& each : styles != nullptr ? styles->items() : std::span<const Value>{}) {
        const Value* id = each.find("styleId");
        const Value* name = each.find("name");
        if (id == nullptr || id->text() == nullptr) {
            continue;
        }
        const std::string kName = name != nullptr && name->text() != nullptr ? *name->text() : *id->text();
        auto added = row(entitiesColumn_, kName, kText, kRow, true);
        if (!added.has_value()) {
            break;
        }
        entityRows_.push_back(*added);
        entities_.push_back(*id->text());
        names_.push_back(kName);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
}

void ShellParticipant::showClass(std::size_t at) {
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
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const Value* chosen = classOf(sheet_, entity_);
    if (chosen == nullptr) {
        return;
    }
    // Its name first, as an entity's is (D638).
    auto named = fieldRow("name", names_[at]);
    if (!named.has_value()) {
        return;
    }
    componentRows_.push_back(named->first);
    fields_.push_back(FieldRow{.value = named->second, .role = FieldRow::Role::Name, .field = "name"});
    // Each variant a heading, in the order a node's states pick them, and
    // under it every part a look has, set or not.
    for (const std::string_view kVariant : ui::kStyleVariantFields) {
        auto heading = row(componentsColumn_, kVariant, kQuiet, kPanel);
        if (!heading.has_value()) {
            return;
        }
        componentRows_.push_back(*heading);
        ++components_;
        const Value* look = chosen->find(kVariant);
        for (const std::string_view kPart : ui::kStyleLookFields) {
            const Value* held = look != nullptr ? look->find(kPart) : nullptr;
            auto line =
                fieldRow(kPart, held != nullptr ? partText(*held) : std::string{}, std::nullopt, held != nullptr);
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            fields_.push_back(FieldRow{.value = line->second,
                                       .role = FieldRow::Role::Part,
                                       .component = std::string{kVariant},
                                       .field = std::string{kPart}});
        }
    }
}

void ShellParticipant::setPart(const FieldRow& row, const std::string& text) {
    Value value;
    if (!text.empty()) {
        auto read = document::parse(text);
        value = read.has_value() ? std::move(*read) : Value::string(text);
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("styles.set_part"));
    operation.add("style", Value::string(entity_));
    operation.add("variant", Value::string(row.component));
    operation.add("part", Value::string(row.field));
    operation.add("value", std::move(value));
    commit(
        std::move(operation), row.component + " " + row.field + (text.empty() ? " unset" : " set to " + text), entity_);
}

} // namespace rawframe::studio
