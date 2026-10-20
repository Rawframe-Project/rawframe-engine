#include "hints.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <map>

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

/// Whether `array`'s items are objects known by nothing of their own, so
/// each is a row named by its place (a clip's tracks and keys, a skeleton's
/// bones, D827).
bool placed(const Value& array) {
    return !array.items().empty() && !keyed(array) && std::ranges::all_of(array.items(), [](const Value& item) {
        return item.kind() == Value::Kind::Object;
    });
}

/// Whether `value` is a row of its own, not a field of its holder's.
bool listed(const Value& value) {
    return value.kind() == Value::Kind::Object ||
           (value.kind() == Value::Kind::Array && (keyed(value) || placed(value)));
}

/// What an item is named, if it says: its name, else its display name.
std::optional<std::string> nameOf(const Value& item) {
    for (const std::string_view kNamed : {"name", "displayName"}) {
        if (const Value* kName = item.find(kNamed); kName != nullptr && kName->kind() == Value::Kind::String) {
            return *kName->text();
        }
    }
    return std::nullopt;
}

/// How a row names an item: its name where it has one, else its key,
/// followed by the array it is in, as `aim (actions)`.
std::string labelOf(const Value& item, const std::string& key) {
    return nameOf(item).value_or(*item.find(key)->text());
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
    // A clip's tracks by their bones and channels, its keys by their times
    // (D939).
    const Value* kSheetKind = sheet_->find("kind");
    const std::map<std::string, std::string, std::less<>> kBones =
        kSheetKind != nullptr && kSheetKind->text() != nullptr && *kSheetKind->text() == "animation.clip"
            ? boneNamesOf(*sheet_)
            : std::map<std::string, std::string, std::less<>>{};
    const bool kClip =
        kSheetKind != nullptr && kSheetKind->text() != nullptr && *kSheetKind->text() == "animation.clip";
    const auto kClipLabel = [&kBones, kClip](const Value& item, const std::string& member, std::size_t place) {
        const Value* kBone = item.find("bone");
        const Value* kChannel = item.find("channel");
        const Value* kTime = item.find("time");
        if (kClip && member == "tracks" && kBone != nullptr && kBone->text() != nullptr) {
            const auto kNamed = kBones.find(*kBone->text());
            return std::optional<std::string>{
                (kNamed != kBones.end() ? kNamed->second : "track " + std::to_string(place + 1)) + " · " +
                (kChannel != nullptr && kChannel->text() != nullptr ? *kChannel->text() : std::string{}) + " (tracks)"};
        }
        if (kClip && member == "keys" && kTime != nullptr && kTime->real().has_value()) {
            char said[32];
            std::snprintf(said, sizeof said, "%g", *kTime->real());
            return std::optional<std::string>{"key at " + std::string{said} + " s (keys)"};
        }
        return std::optional<std::string>{};
    };
    // The document first, then each object and keyed item in it before
    // what it holds, as the document nests them.
    const auto kList = [this, &kClipLabel](const auto& self,
                                           const Value& at,
                                           const std::string& path,
                                           const std::string& label,
                                           std::size_t depth) -> void {
        // A tree row as the design's (D923): what it is named, and the list
        // it is in quieter, read by its whole label.
        const std::size_t kOpen = label.rfind(" (");
        const bool kInList = kOpen != std::string::npos && label.ends_with(')');
        const bool kHolds = std::ranges::any_of(at.items(), [](const Value& held) {
            return listed(held);
        });
        auto added = entityRow(entitiesColumn_,
                               kInList ? label.substr(0, kOpen) : label,
                               depth,
                               kHolds,
                               depth == 0 ? Icon::Data : Icon::Cube,
                               depth == 0 ? look::kLabelInk : look::kEntityInk,
                               look::kRowInk,
                               kInList ? std::string_view{label}.substr(kOpen + 2, label.size() - kOpen - 3)
                                       : std::string_view{});
        if (!added.has_value() || entityRows_.size() >= EditorsHeld::kMostRows) {
            return;
        }
        static_cast<void>(tree_->setName(*added, label));
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
                // An animation graph's node by its kind, its identity
                // quieter after it (D940).
                const bool kNode = path == "graph" && kHeld.find("type") != nullptr;
                self(self,
                     kHeld,
                     joined(path, kMember),
                     kNode ? graphNodeLabel(kHeld) + " (" + kMember + ")" : kMember,
                     depth + 1);
            } else if (kHeld.kind() == Value::Kind::Array && keyed(kHeld)) {
                for (const Value& kItem : kHeld.items()) {
                    const std::string kKey = *keyOf(kItem);
                    self(self,
                         kItem,
                         joined(path, kMember + "[" + kKey + "=" + *kItem.find(kKey)->text() + "]"),
                         labelOf(kItem, kKey) + " (" + kMember + ")",
                         depth + 1);
                }
            } else if (kHeld.kind() == Value::Kind::Array && placed(kHeld)) {
                // Each by its place, named where it says a name, as
                // `hips (bones)`, else counted from one, as `keys 2`; one
                // whose `parent` is an earlier item's place under it.
                std::vector<std::size_t> under(kHeld.items().size(), 0);
                for (std::size_t item = 0; item < kHeld.items().size(); ++item) {
                    const Value& kItem = kHeld.items()[item];
                    const Value* kParent = kItem.find("parent");
                    const std::optional<std::int64_t> kAt =
                        kParent != nullptr ? kParent->integer() : std::optional<std::int64_t>{};
                    if (kAt.has_value() && *kAt >= 0 && static_cast<std::size_t>(*kAt) < item) {
                        under[item] = under[static_cast<std::size_t>(*kAt)] + 1;
                    }
                    const std::optional<std::string> kName = nameOf(kItem);
                    const std::optional<std::string> kLabelled = kClipLabel(kItem, kMember, item);
                    self(self,
                         kItem,
                         joined(path, kMember + "[" + std::to_string(item) + "]"),
                         kLabelled.has_value() ? *kLabelled
                         : kName.has_value()   ? *kName + " (" + kMember + ")"
                                               : kMember + " " + std::to_string(item + 1),
                         depth + 1 + std::min<std::size_t>(under[item], 8));
                }
            }
        }
    };
    kList(kList, *sheet_, "", std::filesystem::path{scene_}.filename().string(), 0);
    // An animation graph's nodes on the canvas too (D819), and a clip's
    // keys on its timeline (D827).
    const Value* kKind = sheet_->find("kind");
    const Value* kGraph = sheet_->find("graph");
    const std::string kNamed = kKind != nullptr && kKind->kind() == Value::Kind::String ? *kKind->text() : "";
    if (kNamed == "animation.graph" && kGraph != nullptr) {
        showCanvas(*kGraph, "graph.");
    } else if (kNamed == "animation.clip") {
        showTimeline(*sheet_);
    } else if (kNamed == "input.actions") {
        showActionTables();
    } else if (kNamed == "text.strings") {
        showStringTables();
    } else {
        showDataTables();
    }
    // A row chosen at once, the Inspector saying what it holds: the first
    // inside the document.
    if (entityRows_.size() > 1 && (entity_.empty() || !std::ranges::contains(entities_, entity_))) {
        // The first that holds a value of its own, not only rows.
        std::size_t first = 1;
        for (std::size_t each = 1; each < held_.data.size(); ++each) {
            if (std::ranges::any_of(held_.data[each].items(), [](const Value& held) {
                    return !listed(held);
                })) {
                first = each;
                break;
            }
        }
        showDataEntry(first);
    }
    // The bottom dock's views of it, where shown (D927).
    if (dockShown_ == Dock::Curves) {
        showCurves();
    }
    if (dockShown_ == Dock::Mixer && scene_ == held_.mixerDocument) {
        showMixer();
    }
}

namespace {

/// A value as a table's cell says it: text as it reads, a number short, a
/// list of plain values apart by commas, a list of rows by how many.
std::string cellOf(const Value& value) {
    std::string made;
    switch (value.kind()) {
    case Value::Kind::String:
        made = *value.text();
        std::ranges::replace(made, '\n', ' ');
        break;
    case Value::Kind::Number: {
        std::array<char, 32> text{};
        std::snprintf(text.data(), text.size(), "%g", value.real().value_or(0));
        made = text.data();
        break;
    }
    case Value::Kind::Bool:
        made = value.truth().value_or(false) ? "yes" : "no";
        break;
    case Value::Kind::Array:
        if (std::ranges::all_of(value.items(), [](const Value& item) {
                return !listed(item);
            })) {
            for (const Value& kItem : value.items()) {
                made += (made.empty() ? "" : ", ") + cellOf(kItem);
            }
        } else {
            made = std::to_string(value.items().size()) + (value.items().size() == 1 ? " row" : " rows");
        }
        break;
    case Value::Kind::Object:
        made = std::to_string(value.names().size()) + " fields";
        break;
    case Value::Kind::Null:
        break;
    }
    return made.size() > 44 ? made.substr(0, 43) + "\u2026" : made;
}

} // namespace

void ShellParticipant::showDataTables() {
    clear(viewRows_);
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
    if (!sheet_.has_value()) {
        return;
    }
    auto pane = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0),
                               .minHeight = ui::pixels(0),
                               .direction = ui::Direction::Column,
                               .gap = 18,
                               .grow = 1,
                               .padding = {20, 20, 16, 16},
                               .scroll = ui::Scroll::Vertical},
                    0);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Tables"));
    std::size_t tables = 0;
    for (std::size_t each = 0; each < sheet_->names().size(); ++each) {
        const std::string& kMember = sheet_->names()[each];
        const Value& kList = sheet_->items()[each];
        // A keyed list, or a map of rows by their names (a strings
        // document's entries).
        const bool kKeyed = kList.kind() == Value::Kind::Array && keyed(kList);
        const bool kMap = kList.kind() == Value::Kind::Object && !kList.names().empty() &&
                          std::ranges::all_of(kList.items(), [](const Value& item) {
                              return item.kind() == Value::Kind::Object;
                          });
        if (!kKeyed && !kMap) {
            continue;
        }
        // Its columns: the key, then the plain values its rows hold, the
        // first rows' order, a few.
        std::vector<std::string> columns;
        const std::string kKey = kKeyed ? *keyOf(kList.items().front()) : std::string{"key"};
        columns.push_back(kKey);
        for (const Value& kItem : kList.items()) {
            for (std::size_t field = 0; field < kItem.names().size() && columns.size() < 6; ++field) {
                if (!std::ranges::contains(columns, kItem.names()[field]) &&
                    (!listed(kItem.items()[field]) || kItem.items()[field].kind() == Value::Kind::Array)) {
                    columns.push_back(kItem.names()[field]);
                }
            }
        }
        auto table = box(*pane, ui::Layout{.direction = ui::Direction::Column, .shrink = 0}, 0);
        if (!table.has_value()) {
            return;
        }
        ++tables;
        if (auto title =
                box(*table,
                    ui::Layout{.alignItems = ui::Align::Center, .gap = 10, .shrink = 0, .padding = {2, 2, 0, 8}},
                    0);
            title.has_value()) {
            if (auto named = box(*title, ui::Layout{.shrink = 0}, 0); named.has_value()) {
                static_cast<void>(strongWords(*named, prettyField(kMember), look::kInk, 17));
            }
            if (auto counted = box(*title, ui::Layout{.shrink = 0}, 0); counted.has_value()) {
                static_cast<void>(
                    words(*counted,
                          std::to_string(kList.items().size()) + (kList.items().size() == 1 ? " row" : " rows"),
                          look::kFaintInk,
                          look::kLabelSize));
            }
        }
        auto frame =
            box(*table, ui::Layout{.direction = ui::Direction::Column, .shrink = 0, .border = {1, 1, 1, 1}}, 0);
        if (!frame.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(
            *frame, ui::Look{.fill = 0x17181CFFU, .borderColor = look::kEdge, .radius = 6, .clip = true}));
        const auto kLine = [&](bool heading, std::uint32_t fill) -> std::optional<ui::Node> {
            auto line = box(*frame,
                            ui::Layout{.height = ui::pixels(heading ? 30 : 34),
                                       .alignItems = ui::Align::Center,
                                       .gap = 12,
                                       .shrink = 0,
                                       .padding = {12, 12, 0, 0},
                                       .border = {0, 0, 0, 1}},
                            0);
            if (!line.has_value()) {
                return std::nullopt;
            }
            static_cast<void>(tree_->setLook(*line, ui::Look{.fill = fill, .borderColor = look::kLine}));
            return *line;
        };
        const auto kCell = [this](ui::Node line, std::string_view text, std::uint32_t ink, bool mono, bool strong) {
            auto cell = box(line, ui::Layout{.width = ui::pixels(0), .grow = 1, .shrink = 1}, 0);
            if (!cell.has_value()) {
                return;
            }
            static_cast<void>(tree_->setLook(*cell, ui::Look{.clip = true}));
            static_cast<void>(strong ? strongWords(*cell, text, ink, look::kBodySize)
                              : mono ? monoWords(*cell, text, ink, look::kSmallSize)
                                     : words(*cell, text, ink, look::kBodySize));
            static_cast<void>(tree_->setInteraction(*cell, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        };
        if (auto heading = kLine(true, 0); heading.has_value()) {
            for (const std::string& kColumn : columns) {
                std::string upper = prettyField(kColumn);
                std::ranges::transform(upper, upper.begin(), [](char letter) {
                    return static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
                });
                kCell(*heading, upper, look::kFaintInk, true, false);
            }
        }
        for (std::size_t item = 0; item < kList.items().size(); ++item) {
            const Value& kItem = kList.items()[item];
            // Each item by its own key, as the Hierarchy names it: items of
            // one list may name theirs differently.
            const std::optional<std::string> kOwnKey = kKeyed ? keyOf(kItem) : std::nullopt;
            if (kKeyed && !kOwnKey.has_value()) {
                continue;
            }
            const std::string kPath = kKeyed ? kMember + "[" + *kOwnKey + "=" + *kItem.find(*kOwnKey)->text() + "]"
                                             : joined(kMember, kList.names()[item]);
            const auto kListed = std::ranges::find(entities_, kPath);
            if (kListed == entities_.end()) {
                continue;
            }
            const auto kEntity = static_cast<std::size_t>(kListed - entities_.begin());
            auto line = kLine(false, kPath == entity_ ? kChosen : 0U);
            if (!line.has_value()) {
                return;
            }
            held_.canvasParts.emplace_back(*line, kEntity);
            held_.canvasBoxes.emplace_back(*line, kEntity);
            static_cast<void>(tree_->setRole(*line, ui::Role::Button));
            static_cast<void>(
                tree_->setName(*line, (kKeyed ? labelOf(kItem, *kOwnKey) : kList.names()[item]) + " row"));
            for (std::size_t column = 0; column < columns.size(); ++column) {
                const Value* kValue = kItem.find(columns[column]);
                kCell(*line,
                      !kKeyed && column == 0 ? kList.names()[item]
                      : kValue != nullptr    ? cellOf(*kValue)
                                             : std::string{},
                      column == 0 ? look::kInk : look::kSoftInk,
                      column != 0,
                      column == 0);
            }
        }
    }
    if (tables == 0) {
        if (auto none = box(*pane, ui::Layout{.shrink = 0}, 0); none.has_value()) {
            static_cast<void>(words(*none,
                                    "This document holds no lists of rows. Choose a part in the Hierarchy to edit "
                                    "its values in the Inspector.",
                                    look::kFaintInk,
                                    look::kBodySize));
        }
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
    // A clip's timeline laid out again, the chosen track's curve with it;
    // a graph's canvas marked (D827, D836).
    const Value* kKind = sheet_.has_value() ? sheet_->find("kind") : nullptr;
    const std::string kKindName = kKind != nullptr && kKind->kind() == Value::Kind::String ? *kKind->text() : "";
    if (kKindName == "animation.clip") {
        showTimeline(*sheet_);
    } else if (kKindName == "animation.graph") {
        markCanvas();
    } else if (kKindName == "input.actions") {
        // The tables again, the chosen row lit (D923, D927).
        showActionTables();
    } else if (kKindName == "text.strings") {
        showStringTables();
    } else {
        showDataTables();
    }
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
        if (listed(kValue)) {
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

namespace {

/// A control's name as a chip says it (D927): a key's letter or word, a
/// pad's control with its side first, `left stick` for `stick_left`.
std::string chipOf(std::string_view control) {
    std::string made{control.starts_with("key_") ? control.substr(4) : control};
    for (const std::string_view kSide : {"_left", "_right"}) {
        if (made.size() > kSide.size() && made.ends_with(kSide)) {
            made = std::string{kSide.substr(1)} + " " + made.substr(0, made.size() - kSide.size());
        }
    }
    std::ranges::replace(made, '_', ' ');
    if (made.size() == 1) {
        made[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(made[0])));
    } else if (control.starts_with("key_") && !made.empty()) {
        made[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(made[0])));
    }
    return made;
}

/// What a binding presses, as chips: a quad's four keys, up, left, down,
/// right, as WASD reads; else its key or control.
std::vector<std::string> chipsOf(const Value& binding) {
    std::vector<std::string> made;
    if (const Value* kQuad = binding.find("quad"); kQuad != nullptr) {
        for (const std::string_view kWay : {"up", "left", "down", "right"}) {
            if (const Value* kKey = kQuad->find(kWay); kKey != nullptr && kKey->text() != nullptr) {
                made.push_back(chipOf(*kKey->text()));
            }
        }
        return made;
    }
    for (const std::string_view kNamed : {"physicalKey", "control", "button", "key"}) {
        if (const Value* kKey = binding.find(kNamed); kKey != nullptr && kKey->text() != nullptr) {
            made.push_back(chipOf(*kKey->text()));
            return made;
        }
    }
    return made;
}

} // namespace

void ShellParticipant::showActionTables() {
    clear(viewRows_);
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
    const Value* actions = sheet_.has_value() ? sheet_->find("actions") : nullptr;
    if (actions == nullptr || actions->kind() != Value::Kind::Array) {
        showDataTables();
        return;
    }
    auto pane = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0),
                               .minHeight = ui::pixels(0),
                               .direction = ui::Direction::Column,
                               .gap = 18,
                               .grow = 1,
                               .padding = {20, 20, 16, 16},
                               .scroll = ui::Scroll::Vertical},
                    0);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Actions"));
    // As the design's Data screen: a table for each context, its actions'
    // kinds and what each device presses for them as chips; those in no
    // context last.
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;
    std::vector<std::string> grouped;
    if (const Value* kContexts = sheet_->find("contexts"); kContexts != nullptr) {
        for (const Value& kContext : kContexts->items()) {
            const Value* kName = kContext.find("name");
            const Value* kListed = kContext.find("actions");
            std::vector<std::string> names;
            for (const Value& kAction : kListed != nullptr ? kListed->items() : std::span<const Value>{}) {
                if (kAction.text() != nullptr) {
                    names.push_back(*kAction.text());
                    grouped.push_back(*kAction.text());
                }
            }
            groups.emplace_back(kName != nullptr && kName->text() != nullptr ? *kName->text() : "context",
                                std::move(names));
        }
    }
    std::vector<std::string> rest;
    for (const Value& kAction : actions->items()) {
        const Value* kName = kAction.find("name");
        if (kName != nullptr && kName->text() != nullptr && !std::ranges::contains(grouped, *kName->text())) {
            rest.push_back(*kName->text());
        }
    }
    if (!rest.empty()) {
        groups.emplace_back(groups.empty() ? "Actions" : "In no context", std::move(rest));
    }
    constexpr std::array<std::string_view, 5> kColumns{"ACTION", "KIND", "KEYBOARD AND MOUSE", "GAMEPAD", "TOUCH"};
    constexpr std::array<float, 5> kShares{1.0F, 0.8F, 1.6F, 1.3F, 1.3F};
    for (const auto& [kGroup, kNames] : groups) {
        auto table = box(*pane, ui::Layout{.direction = ui::Direction::Column, .shrink = 0}, 0);
        if (!table.has_value()) {
            return;
        }
        if (auto title =
                box(*table,
                    ui::Layout{.alignItems = ui::Align::Center, .gap = 10, .shrink = 0, .padding = {2, 2, 0, 8}},
                    0);
            title.has_value()) {
            if (auto named = box(*title, ui::Layout{.shrink = 0}, 0); named.has_value()) {
                static_cast<void>(strongWords(*named, prettyField(kGroup), look::kInk, 17));
            }
            if (auto counted = box(*title, ui::Layout{.shrink = 0}, 0); counted.has_value()) {
                static_cast<void>(words(*counted,
                                        std::to_string(kNames.size()) + (kNames.size() == 1 ? " action" : " actions"),
                                        look::kFaintInk,
                                        look::kLabelSize));
            }
        }
        auto frame =
            box(*table, ui::Layout{.direction = ui::Direction::Column, .shrink = 0, .border = {1, 1, 1, 1}}, 0);
        if (!frame.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(
            *frame, ui::Look{.fill = 0x17181CFFU, .borderColor = look::kEdge, .radius = 6, .clip = true}));
        const auto kLine = [&](bool heading, std::uint32_t fill) -> std::optional<ui::Node> {
            auto line = box(*frame,
                            ui::Layout{.height = ui::pixels(heading ? 30 : 36),
                                       .alignItems = ui::Align::Center,
                                       .gap = 12,
                                       .shrink = 0,
                                       .padding = {12, 12, 0, 0},
                                       .border = {0, 0, 0, 1}},
                            0);
            if (!line.has_value()) {
                return std::nullopt;
            }
            static_cast<void>(tree_->setLook(*line, ui::Look{.fill = fill, .borderColor = look::kLine}));
            return *line;
        };
        const auto kCell = [this](ui::Node line, float share) -> std::optional<ui::Node> {
            auto cell = box(
                line, ui::Layout{.width = ui::pixels(0), .alignItems = ui::Align::Center, .gap = 4, .grow = share}, 0);
            if (!cell.has_value()) {
                return std::nullopt;
            }
            static_cast<void>(tree_->setLook(*cell, ui::Look{.clip = true}));
            static_cast<void>(tree_->setInteraction(*cell, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            return *cell;
        };
        if (auto heading = kLine(true, 0); heading.has_value()) {
            for (std::size_t column = 0; column < kColumns.size(); ++column) {
                if (auto cell = kCell(*heading, kShares[column]); cell.has_value()) {
                    static_cast<void>(monoWords(*cell, kColumns[column], look::kFaintInk, look::kSmallSize));
                }
            }
        }
        for (const std::string& kName : kNames) {
            const auto kAction = std::ranges::find_if(actions->items(), [&kName](const Value& action) {
                const Value* kNamed = action.find("name");
                return kNamed != nullptr && kNamed->text() != nullptr && *kNamed->text() == kName;
            });
            const Value* kId = kAction != actions->items().end() ? kAction->find("actionId") : nullptr;
            if (kId == nullptr || kId->text() == nullptr) {
                continue;
            }
            const std::string kPath = "actions[actionId=" + *kId->text() + "]";
            const auto kListed = std::ranges::find(entities_, kPath);
            if (kListed == entities_.end()) {
                continue;
            }
            const auto kEntity = static_cast<std::size_t>(kListed - entities_.begin());
            auto line = kLine(false, kPath == entity_ ? kChosen : 0U);
            if (!line.has_value()) {
                return;
            }
            held_.canvasParts.emplace_back(*line, kEntity);
            held_.canvasBoxes.emplace_back(*line, kEntity);
            static_cast<void>(tree_->setRole(*line, ui::Role::Button));
            static_cast<void>(tree_->setName(*line, kName + " row"));
            if (auto cell = kCell(*line, kShares[0]); cell.has_value()) {
                static_cast<void>(strongWords(*cell, kName, look::kInk, look::kBodySize));
            }
            if (auto cell = kCell(*line, kShares[1]); cell.has_value()) {
                const Value* kType = kAction->find("valueType");
                static_cast<void>(monoWords(*cell,
                                            kType != nullptr && kType->text() != nullptr ? *kType->text() : "",
                                            look::kSoftInk,
                                            look::kSmallSize));
            }
            const Value* kBindings = kAction->find("bindings");
            for (std::size_t column = 2; column < kColumns.size(); ++column) {
                auto cell = kCell(*line, kShares[column]);
                if (!cell.has_value()) {
                    return;
                }
                std::size_t shown = 0;
                for (const Value& kBinding : kBindings != nullptr ? kBindings->items() : std::span<const Value>{}) {
                    const Value* kDevice = kBinding.find("device");
                    const std::string_view kOn =
                        kDevice != nullptr && kDevice->text() != nullptr ? std::string_view{*kDevice->text()} : "";
                    const std::size_t kColumn = kOn == "gamepad" ? 3 : kOn == "touch" ? 4 : 2;
                    if (kColumn != column) {
                        continue;
                    }
                    for (const std::string& kChip : chipsOf(kBinding)) {
                        auto chip = box(*cell,
                                        ui::Layout{.height = ui::pixels(22),
                                                   .alignItems = ui::Align::Center,
                                                   .shrink = 0,
                                                   .padding = {6, 6, 0, 0},
                                                   .border = {1, 1, 1, 1}},
                                        0);
                        if (!chip.has_value()) {
                            return;
                        }
                        static_cast<void>(tree_->setLook(
                            *chip, ui::Look{.fill = look::kWellFill, .borderColor = look::kEdge, .radius = 4}));
                        static_cast<void>(monoWords(*chip, kChip, look::kInk, look::kSmallSize));
                        ++shown;
                    }
                }
                if (shown == 0) {
                    static_cast<void>(monoWords(*cell, "none", look::kFaintInk, look::kSmallSize));
                }
            }
        }
    }
}

namespace {

/// A locale's name in its own words, where Studio knows it; else its code.
std::string languageOf(std::string_view locale) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 9> kKnown{{{"en", "English"},
                                                                                   {"tr", "T\u00FCrk\u00E7e"},
                                                                                   {"de", "Deutsch"},
                                                                                   {"fr", "Fran\u00E7ais"},
                                                                                   {"es", "Espa\u00F1ol"},
                                                                                   {"it", "Italiano"},
                                                                                   {"pt", "Portugu\u00EAs"},
                                                                                   {"nl", "Nederlands"},
                                                                                   {"pl", "Polski"}}};
    const auto kFound =
        std::ranges::find(kKnown, locale.substr(0, 2), &std::pair<std::string_view, std::string_view>::first);
    return kFound != kKnown.end() ? std::string{kFound->second} : std::string{locale};
}

/// A message as its cell shows it: on one line, at most 44 characters.
std::string messageCell(const Value* entry) {
    const Value* kMessage = entry != nullptr ? entry->find("message") : nullptr;
    if (kMessage == nullptr || kMessage->text() == nullptr) {
        return {};
    }
    std::string made = *kMessage->text();
    std::ranges::replace(made, '\n', ' ');
    return made.size() > 44 ? made.substr(0, 43) + "\u2026" : made;
}

} // namespace

void ShellParticipant::showStringTables() {
    clear(viewRows_);
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
    const Value* entries = sheet_.has_value() ? sheet_->find("entries") : nullptr;
    if (entries == nullptr || entries->kind() != Value::Kind::Object) {
        showDataTables();
        return;
    }
    // Its translations: the game's documents named as this one with a
    // locale before `.translations`, as `gale.tr.translations` is
    // `gale.strings`' Turkish (D927).
    const std::string kStem = std::filesystem::path{scene_}.stem().string();
    const std::string kFolder = std::filesystem::path{scene_}.parent_path().generic_string();
    std::vector<std::pair<std::string, Value>> translations;
    for (const std::string& kDocument : scenes_) {
        const std::filesystem::path kPath{kDocument};
        if (kPath.extension() != ".translations" || kPath.parent_path().generic_string() != kFolder ||
            !kPath.stem().string().starts_with(kStem + ".")) {
            continue;
        }
        const std::optional<Value> kRead = read(kDocument, "data.read");
        const Value* kData = kRead.has_value() ? kRead->find("data") : nullptr;
        const Value* kLocale = kData != nullptr ? kData->find("locale") : nullptr;
        if (kData != nullptr && kLocale != nullptr && kLocale->text() != nullptr) {
            translations.emplace_back(*kLocale->text(), *kData);
        }
    }
    auto pane = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0),
                               .minHeight = ui::pixels(0),
                               .direction = ui::Direction::Column,
                               .gap = 12,
                               .grow = 1,
                               .padding = {20, 20, 16, 16},
                               .scroll = ui::Scroll::Vertical},
                    0);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Strings"));
    const Value* kSource = sheet_->find("sourceLocale");
    const std::string kSourceLocale = kSource != nullptr && kSource->text() != nullptr ? *kSource->text() : "en";
    std::size_t missing = 0;
    for (const auto& [kLocale, kData] : translations) {
        const Value* kHeld = kData.find("entries");
        for (const std::string& kKey : entries->names()) {
            missing += kHeld == nullptr || kHeld->find(kKey) == nullptr ? 1 : 0;
        }
    }
    if (auto title =
            box(*pane, ui::Layout{.alignItems = ui::Align::Center, .gap = 10, .shrink = 0, .padding = {2, 2, 0, 0}}, 0);
        title.has_value()) {
        if (auto named = box(*title, ui::Layout{.shrink = 0}, 0); named.has_value()) {
            static_cast<void>(strongWords(*named, "Strings", look::kInk, 17));
        }
        if (auto counted = box(*title, ui::Layout{.shrink = 0}, 0); counted.has_value()) {
            static_cast<void>(words(*counted,
                                    std::to_string(entries->names().size()) + " keys, " +
                                        std::to_string(translations.size()) +
                                        (translations.size() == 1 ? " translation, " : " translations, ") +
                                        std::to_string(missing) + " missing",
                                    missing > 0 ? look::kWarn : look::kFaintInk,
                                    look::kLabelSize));
        }
    }
    auto frame = box(*pane, ui::Layout{.direction = ui::Direction::Column, .shrink = 0, .border = {1, 1, 1, 1}}, 0);
    if (!frame.has_value()) {
        return;
    }
    static_cast<void>(
        tree_->setLook(*frame, ui::Look{.fill = 0x17181CFFU, .borderColor = look::kEdge, .radius = 6, .clip = true}));
    const auto kLine = [&](bool heading, std::uint32_t fill) -> std::optional<ui::Node> {
        auto line = box(*frame,
                        ui::Layout{.height = ui::pixels(heading ? 30 : 32),
                                   .alignItems = ui::Align::Center,
                                   .gap = 12,
                                   .shrink = 0,
                                   .padding = {12, 12, 0, 0},
                                   .border = {0, 0, 0, 1}},
                        0);
        if (!line.has_value()) {
            return std::nullopt;
        }
        static_cast<void>(tree_->setLook(*line, ui::Look{.fill = fill, .borderColor = look::kLine}));
        return *line;
    };
    const auto kCell = [this](ui::Node line, float share, std::uint32_t fill) -> std::optional<ui::Node> {
        auto cell = box(line,
                        ui::Layout{.width = ui::pixels(share == 0 ? 16 : 0),
                                   .height = ui::share(1),
                                   .alignItems = ui::Align::Center,
                                   .grow = share,
                                   .shrink = share == 0 ? 0.0F : 1.0F,
                                   .padding = {4, 4, 0, 0}},
                        fill);
        if (!cell.has_value()) {
            return std::nullopt;
        }
        static_cast<void>(tree_->setLook(*cell, ui::Look{.fill = fill, .clip = true}));
        static_cast<void>(tree_->setInteraction(*cell, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        // What it says in a child of its own height, which the cell
        // centers (no text is centered upright by itself).
        auto inner = box(*cell, ui::Layout{.shrink = 1}, 0);
        if (inner.has_value()) {
            static_cast<void>(tree_->setInteraction(*inner, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        return inner.has_value() ? std::optional<ui::Node>{*inner} : std::nullopt;
    };
    if (auto heading = kLine(true, 0); heading.has_value()) {
        static_cast<void>(kCell(*heading, 0, 0));
        if (auto cell = kCell(*heading, 1.0F, 0); cell.has_value()) {
            static_cast<void>(words(*cell, "Key", look::kFaintInk, look::kLabelSize));
        }
        if (auto cell = kCell(*heading, 1.4F, 0); cell.has_value()) {
            static_cast<void>(words(*cell, languageOf(kSourceLocale) + " source", look::kFaintInk, look::kLabelSize));
        }
        for (const auto& [kLocale, kData] : translations) {
            const Value* kHeld = kData.find("entries");
            std::size_t done = 0;
            for (const std::string& kKey : entries->names()) {
                done += kHeld != nullptr && kHeld->find(kKey) != nullptr ? 1 : 0;
            }
            if (auto cell = kCell(*heading, 1.4F, 0); cell.has_value()) {
                static_cast<void>(words(
                    *cell,
                    languageOf(kLocale) + " " +
                        std::to_string(entries->names().empty() ? 100 : done * 100 / entries->names().size()) + " %",
                    look::kFaintInk,
                    look::kLabelSize));
            }
        }
    }
    for (std::size_t item = 0; item < entries->names().size(); ++item) {
        const std::string& kKey = entries->names()[item];
        const std::string kPath = "entries." + kKey;
        const auto kListed = std::ranges::find(entities_, kPath);
        if (kListed == entities_.end()) {
            continue;
        }
        const auto kEntity = static_cast<std::size_t>(kListed - entities_.begin());
        auto line = kLine(false, kPath == entity_ ? kChosen : 0U);
        if (!line.has_value()) {
            return;
        }
        held_.canvasParts.emplace_back(*line, kEntity);
        held_.canvasBoxes.emplace_back(*line, kEntity);
        static_cast<void>(tree_->setRole(*line, ui::Role::Button));
        static_cast<void>(tree_->setName(*line, kKey + " row"));
        const bool kWhole = std::ranges::all_of(translations, [&kKey](const std::pair<std::string, Value>& each) {
            const Value* kHeld = each.second.find("entries");
            return kHeld != nullptr && kHeld->find(kKey) != nullptr;
        });
        if (auto cell = kCell(*line, 0, 0); cell.has_value()) {
            static_cast<void>(
                icon(*cell, kWhole ? Icon::Check : Icon::Warning, kWhole ? look::kReady : look::kWarn, 13));
        }
        if (auto cell = kCell(*line, 1.0F, 0); cell.has_value()) {
            static_cast<void>(monoWords(*cell, kKey, look::kSoftInk, look::kSmallSize));
        }
        if (auto cell = kCell(*line, 1.4F, 0); cell.has_value()) {
            static_cast<void>(words(*cell, messageCell(&entries->items()[item]), look::kInk, look::kBodySize));
        }
        for (const auto& [kLocale, kData] : translations) {
            const Value* kHeld = kData.find("entries");
            const Value* kEntry = kHeld != nullptr ? kHeld->find(kKey) : nullptr;
            if (auto cell = kCell(*line, 1.4F, kEntry == nullptr ? 0x2A2214FFU : 0U); cell.has_value()) {
                static_cast<void>(kEntry != nullptr ? words(*cell, messageCell(kEntry), look::kInk, look::kBodySize)
                                                    : words(*cell, "missing", look::kWarn, look::kBodySize));
            }
        }
    }
}

} // namespace rawframe::studio
