#include "rawframe/input/overrides.h"

#include "binding_document.h"
#include "rawframe/document/errors.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <utility>

namespace rawframe::input {

namespace {

using document::invalid;
using document::Record;
using document::Value;

constexpr std::array<std::string_view, 4> kDocumentFields = {"kind", "formatVersion", "target", "entries"};
constexpr std::array<std::string_view, 4> kEntryFields = {"actionId", "device", "slot", "binding"};

auto keyOf(const OverrideEntry& entry) noexcept {
    return std::tuple{entry.action, entry.device, entry.slot};
}

std::string identityText(std::uint64_t identity) {
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string text(16, '0');
    for (std::size_t digit = 0; digit < 16; ++digit) {
        text[15 - digit] = kHex[(identity >> (digit * 4)) & 0xFU];
    }
    return text;
}

/// Whether any control of the binding is reserved.
std::optional<Control> reservedIn(const ActionSet& set, const Binding& binding) noexcept {
    for (const Control kControl : binding.controls) {
        if (kControl.valid() && std::ranges::contains(set.reserved, kControl)) {
            return kControl;
        }
    }
    return std::nullopt;
}

/// Puts `entry` in its place by key, replacing one with the same key.
void place(std::vector<OverrideEntry>& entries, OverrideEntry entry) {
    const auto kAt = std::ranges::lower_bound(entries, keyOf(entry), {}, keyOf);
    if (kAt != entries.end() && keyOf(*kAt) == keyOf(entry)) {
        *kAt = std::move(entry);
    } else {
        entries.insert(kAt, std::move(entry));
    }
}

result::Status checkSlot(const ActionSet& set,
                         std::uint64_t action,
                         std::uint32_t slot,
                         const ActionSetLimits& limits,
                         const std::string& path) {
    if (!set.actionWithId(action)) {
        return invalid(path, "no action has that identity");
    }
    if (slot >= limits.maximumSlotsPerDeviceClass) {
        return invalid(path, "the slot is past the limit of slots per device class");
    }
    return {};
}

} // namespace

result::Status
Overrides::rebind(const ActionSet& set, std::uint64_t action, const Binding& binding, const ActionSetLimits& limits) {
    RAWFRAME_TRY(checkSlot(set, action, binding.slot, limits, "binding"));
    // Checked as a document would be: written, then read.
    const Action& owner = set.actions[*set.actionWithId(action)];
    RAWFRAME_TRY_ASSIGN(const Binding kChecked,
                        readBinding(bindingDocument(binding, true), owner, "binding", &binding));
    if (reservedIn(set, kChecked)) {
        return invalid("binding", "the control is reserved");
    }
    place(entries,
          OverrideEntry{.action = action,
                        .device = binding.device,
                        .slot = binding.slot,
                        .kind = OverrideEntry::Kind::Replace,
                        .binding = kChecked,
                        .orphan = {}});
    return {};
}

result::Status Overrides::disable(
    const ActionSet& set, std::uint64_t action, DeviceClass device, std::uint32_t slot, const ActionSetLimits& limits) {
    RAWFRAME_TRY(checkSlot(set, action, slot, limits, "slot"));
    place(entries,
          OverrideEntry{.action = action,
                        .device = device,
                        .slot = slot,
                        .kind = OverrideEntry::Kind::Disable,
                        .binding = {},
                        .orphan = {}});
    return {};
}

void Overrides::reset(std::uint64_t action, DeviceClass device, std::uint32_t slot) {
    std::erase_if(entries, [&](const OverrideEntry& entry) {
        return keyOf(entry) == std::tuple{action, device, slot} && entry.kind != OverrideEntry::Kind::Orphaned;
    });
}

void Overrides::resetAll() {
    std::erase_if(entries, [](const OverrideEntry& entry) {
        return entry.kind != OverrideEntry::Kind::Orphaned;
    });
}

result::Result<Overrides>
readOverrides(std::string_view text, const ActionSet& set, std::string_view target, const ActionSetLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Value kRoot, document::parseCanonical(text));
    const Value* version = kRoot.find("formatVersion");
    if (version == nullptr || version->integer() != 1) {
        return invalid("formatVersion", "the format version is 1");
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(kRoot, kDocumentFields, "$"));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    if (kKind != "input.overrides") {
        return invalid(kRecord.pathOf("kind"), "the kind is input.overrides");
    }
    Overrides overrides;
    RAWFRAME_TRY_ASSIGN(const std::string_view kTarget, kRecord.text("target"));
    if (kTarget != target) {
        return invalid(kRecord.pathOf("target"), "the overrides are for another action set");
    }
    overrides.target = std::string{kTarget};
    RAWFRAME_TRY_ASSIGN(const Value* entries, kRecord.optional("entries", Value::Kind::Array));
    if (entries == nullptr) {
        return overrides;
    }
    if (entries->items().empty()) {
        return document::notCanonical(kRecord.pathOf("entries"), "a field at its default is omitted");
    }
    for (std::size_t index = 0; index < entries->items().size(); ++index) {
        const Value& value = entries->items()[index];
        const std::string kPath = kRecord.pathOf("entries") + "[" + std::to_string(index) + "]";
        RAWFRAME_TRY_ASSIGN(const Record kEntry, Record::of(value, kEntryFields, kPath));
        OverrideEntry entry;
        RAWFRAME_TRY_ASSIGN(const std::string_view kAction, kEntry.text("actionId"));
        const auto kId = parseIdentity(kAction);
        if (!kId) {
            return invalid(kEntry.pathOf("actionId"), "an identity is 16 lowercase hexadecimal digits");
        }
        entry.action = *kId;
        RAWFRAME_TRY_ASSIGN(const std::string_view kDevice, kEntry.text("device"));
        const auto kClass = deviceClassNamed(kDevice);
        if (!kClass) {
            return invalid(kEntry.pathOf("device"), "a device class is keyboard, mouse, or gamepad");
        }
        entry.device = *kClass;
        RAWFRAME_TRY_ASSIGN(const std::int64_t kSlot, kEntry.integer("slot", 0));
        if (kSlot < 0 || kSlot > 0xFFFF) {
            return invalid(kEntry.pathOf("slot"), "a slot is an ordinal below 65536");
        }
        entry.slot = static_cast<std::uint32_t>(kSlot);
        if (!overrides.entries.empty() && keyOf(overrides.entries.back()) >= keyOf(entry)) {
            return document::notCanonical(kPath, "entries are in key order, each key once");
        }
        const auto kOwner = set.actionWithId(entry.action);
        const Value* binding = value.find("binding");
        if (binding == nullptr || (binding->kind() != Value::Kind::Object && binding->kind() != Value::Kind::String)) {
            return invalid(kEntry.pathOf("binding"), "a binding is a record or \"disabled\"");
        }
        if (!kOwner || entry.slot >= limits.maximumSlotsPerDeviceClass) {
            entry.kind = OverrideEntry::Kind::Orphaned;
            entry.orphan = value;
        } else if (binding->kind() == Value::Kind::String) {
            if (*binding->text() != "disabled") {
                return invalid(kEntry.pathOf("binding"), "a binding is a record or \"disabled\"");
            }
            entry.kind = OverrideEntry::Kind::Disable;
        } else {
            const Binding kKey{.slot = entry.slot, .device = entry.device};
            RAWFRAME_TRY_ASSIGN(entry.binding,
                                readBinding(*binding, set.actions[*kOwner], kEntry.pathOf("binding"), &kKey));
            if (reservedIn(set, entry.binding)) {
                return invalid(kEntry.pathOf("binding"), "the control is reserved");
            }
        }
        overrides.entries.push_back(std::move(entry));
    }
    return overrides;
}

std::string writeOverrides(const Overrides& overrides) {
    Value root = Value::object();
    root.add("kind", Value::string("input.overrides"));
    root.add("formatVersion", Value::integer(1));
    root.add("target", Value::string(overrides.target));
    if (!overrides.entries.empty()) {
        Value entries = Value::array();
        for (const OverrideEntry& entry : overrides.entries) {
            if (entry.kind == OverrideEntry::Kind::Orphaned) {
                entries.push(entry.orphan);
                continue;
            }
            Value record = Value::object();
            record.add("actionId", Value::string(identityText(entry.action)));
            record.add("device", Value::string(std::string{nameOf(entry.device)}));
            if (entry.slot != 0) {
                record.add("slot", Value::integer(entry.slot));
            }
            record.add("binding",
                       entry.kind == OverrideEntry::Kind::Disable ? Value::string("disabled")
                                                                  : bindingDocument(entry.binding, true));
            entries.push(std::move(record));
        }
        root.add("entries", std::move(entries));
    }
    return document::write(root);
}

ActionSet applyOverrides(const ActionSet& defaults, const Overrides& overrides) {
    ActionSet applied = defaults;
    for (const OverrideEntry& entry : overrides.entries) {
        const auto kOwner = applied.actionWithId(entry.action);
        if (!kOwner || entry.kind == OverrideEntry::Kind::Orphaned) {
            continue;
        }
        std::vector<Binding>& bindings = applied.actions[*kOwner].bindings;
        const auto kSame = std::ranges::find_if(bindings, [&entry](const Binding& binding) {
            return binding.device == entry.device && binding.slot == entry.slot;
        });
        if (entry.kind == OverrideEntry::Kind::Disable) {
            if (kSame != bindings.end()) {
                bindings.erase(kSame);
            }
        } else if (kSame != bindings.end()) {
            *kSame = entry.binding;
        } else {
            bindings.push_back(entry.binding);
        }
    }
    return applied;
}

} // namespace rawframe::input
