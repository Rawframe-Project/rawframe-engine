#include "rawframe/input/actions.h"

#include "binding_document.h"
#include "rawframe/document/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace rawframe::input {

namespace {

using document::invalid;
using document::Record;
using document::Value;

constexpr std::array<std::string_view, 5> kDocumentFields = {
    "kind", "formatVersion", "actions", "contexts", "reserved"};
constexpr std::array<std::string_view, 9> kActionFields = {"actionId",
                                                           "name",
                                                           "valueType",
                                                           "displayName",
                                                           "group",
                                                           "pressThreshold",
                                                           "releaseThreshold",
                                                           "consume",
                                                           "bindings"};
constexpr std::array<std::string_view, 15> kBindingFields = {"slot",
                                                             "device",
                                                             "physicalKey",
                                                             "logicalKey",
                                                             "control",
                                                             "pair",
                                                             "quad",
                                                             "mode",
                                                             "modifiers",
                                                             "exactModifiers",
                                                             "deadzoneLower",
                                                             "deadzoneUpper",
                                                             "invertX",
                                                             "invertY",
                                                             "scale"};
constexpr std::array<std::string_view, 2> kPairFields = {"negative", "positive"};
constexpr std::array<std::string_view, 4> kQuadFields = {"up", "down", "left", "right"};
constexpr std::array<std::string_view, 5> kContextFields = {
    "contextId", "name", "priority", "actions", "textEditGated"};
constexpr std::array<std::string_view, 3> kReservedFields = {"device", "physicalKey", "control"};

std::string indexed(const std::string& path, std::size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

/// A machine name: `^[a-z][a-z0-9_]*$`, within the limit.
bool machineName(std::string_view name, std::size_t limit) noexcept {
    if (name.empty() || name.size() > limit || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    return std::ranges::all_of(name, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
    });
}

result::Result<std::uint64_t> identityOf(const Record& record, std::string_view field) {
    RAWFRAME_TRY_ASSIGN(const std::string_view kText, record.text(field));
    const auto kId = parseIdentity(kText);
    if (!kId) {
        return invalid(record.pathOf(field), "an identity is 16 lowercase hexadecimal digits");
    }
    return *kId;
}

result::Result<std::string> nameOf(const Record& record, std::string_view field, std::size_t limit) {
    RAWFRAME_TRY_ASSIGN(const std::string_view kName, record.text(field));
    if (!machineName(kName, limit)) {
        return invalid(record.pathOf(field), "a name is a lowercase letter, then letters, digits, or underscores");
    }
    return std::string{kName};
}

/// One control of `device` named by the string at `path`.
result::Result<Control> controlAt(const Value& value, DeviceClass device, const std::string& path) {
    if (value.kind() != Value::Kind::String) {
        return invalid(path, "a control is named by a string");
    }
    const auto kControl = controlNamed(device, *value.text());
    if (!kControl) {
        return invalid(path, "no such control on this device class");
    }
    return *kControl;
}

/// A single control, named by `physicalKey` on a keyboard and `control`
/// elsewhere; the other must be absent.
result::Result<std::optional<Control>> singleControl(const Record& record, DeviceClass device) {
    RAWFRAME_TRY_ASSIGN(const Value* key, record.optional("physicalKey", Value::Kind::String));
    RAWFRAME_TRY_ASSIGN(const Value* control, record.optional("control", Value::Kind::String));
    if (device == DeviceClass::Keyboard && control != nullptr) {
        return invalid(record.pathOf("control"), "a keyboard binding names a physicalKey");
    }
    if (device != DeviceClass::Keyboard && key != nullptr) {
        return invalid(record.pathOf("physicalKey"), "only a keyboard binding names a physicalKey");
    }
    const Value* named = key != nullptr ? key : control;
    if (named == nullptr) {
        return std::optional<Control>{};
    }
    RAWFRAME_TRY_ASSIGN(const Control kControl,
                        controlAt(*named, device, record.pathOf(key != nullptr ? "physicalKey" : "control")));
    return std::optional{kControl};
}

result::Result<std::uint32_t> readSlot(const Record& record) {
    RAWFRAME_TRY_ASSIGN(const std::int64_t kSlot, record.integer("slot", 0));
    if (kSlot < 0 || kSlot > 0xFFFF) {
        return invalid(record.pathOf("slot"), "a slot is an ordinal below 65536");
    }
    return static_cast<std::uint32_t>(kSlot);
}

result::Result<DeviceClass> readDeviceClass(const Record& record) {
    RAWFRAME_TRY_ASSIGN(const std::string_view kDevice, record.text("device"));
    const auto kClass = deviceClassNamed(kDevice);
    if (!kClass) {
        return invalid(record.pathOf("device"), "a device class is keyboard, mouse, or gamepad");
    }
    return *kClass;
}

} // namespace

result::Result<Binding>
readBinding(const Value& value, const Action& action, const std::string& path, const Binding* key) {
    RAWFRAME_TRY_ASSIGN(const Record kRecord,
                        Record::of(value,
                                   key != nullptr ? std::span<const std::string_view>{kBindingFields}.subspan(2)
                                                  : std::span<const std::string_view>{kBindingFields},
                                   path));
    Binding binding;
    if (key != nullptr) {
        binding.slot = key->slot;
        binding.device = key->device;
    } else {
        RAWFRAME_TRY_ASSIGN(binding.slot, readSlot(kRecord));
        RAWFRAME_TRY_ASSIGN(binding.device, readDeviceClass(kRecord));
    }
    RAWFRAME_TRY_ASSIGN(const Value* logical, kRecord.optional("logicalKey", Value::Kind::String));
    if (logical != nullptr) {
        return invalid(kRecord.pathOf("logicalKey"), "logical keys are not read yet; bind the physical key");
    }

    // Exactly one of a control, a pair, or a quad.
    RAWFRAME_TRY_ASSIGN(const std::optional<Control> kSingle, singleControl(kRecord, binding.device));
    RAWFRAME_TRY_ASSIGN(const Value* pair, kRecord.optional("pair", Value::Kind::Object));
    RAWFRAME_TRY_ASSIGN(const Value* quad, kRecord.optional("quad", Value::Kind::Object));
    if ((kSingle ? 1 : 0) + (pair != nullptr ? 1 : 0) + (quad != nullptr ? 1 : 0) != 1) {
        return invalid(path, "a binding names one control, one pair, or one quad");
    }
    if (kSingle) {
        binding.controls[0] = *kSingle;
    } else {
        const bool kPair = pair != nullptr;
        binding.composite = kPair ? Composite::Pair : Composite::Quad;
        const std::span<const std::string_view> kParts =
            kPair ? std::span<const std::string_view>{kPairFields} : std::span<const std::string_view>{kQuadFields};
        const std::string kPath = kRecord.pathOf(kPair ? "pair" : "quad");
        RAWFRAME_TRY_ASSIGN(const Record kPartRecord, Record::of(kPair ? *pair : *quad, kParts, kPath));
        for (std::size_t part = 0; part < kParts.size(); ++part) {
            RAWFRAME_TRY_ASSIGN(const Value* named, kPartRecord.required(kParts[part], Value::Kind::String));
            RAWFRAME_TRY_ASSIGN(binding.controls[part],
                                controlAt(*named, binding.device, kPartRecord.pathOf(kParts[part])));
            if (shapeOf(binding.controls[part]) != ControlShape::Digital) {
                return invalid(kPartRecord.pathOf(kParts[part]), "a composite is made of digital controls");
            }
        }
    }

    // What its controls make must suit the action's type.
    const ControlShape kShape = binding.composite == Composite::Pair   ? ControlShape::Axis1
                                : binding.composite == Composite::Quad ? ControlShape::Axis2
                                                                       : shapeOf(binding.controls[0]);
    const bool kComposite = binding.composite != Composite::None;
    const bool kSuits =
        kComposite
            ? (kShape == ControlShape::Axis1 ? action.type == ValueType::Axis1D : action.type == ValueType::Axis2D)
            : (kShape == ControlShape::Axis2 ? action.type != ValueType::Axis1D : action.type != ValueType::Axis2D);
    if (!kSuits) {
        return invalid(path, "the binding's controls do not make the action's value type");
    }

    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kMode, kRecord.optionalText("mode"));
    if (kMode) {
        if (binding.composite != Composite::Quad) {
            return invalid(kRecord.pathOf("mode"), "only a quad has a mode");
        }
        if (*kMode == "digital_normalized") {
            return document::notCanonical(kRecord.pathOf("mode"), "a field at its default is omitted");
        }
        if (*kMode != "digital") {
            return invalid(kRecord.pathOf("mode"), "a quad's mode is digital or digital_normalized");
        }
        binding.mode = CompositeMode::Digital;
    }

    RAWFRAME_TRY_ASSIGN(const Value* modifiers, kRecord.optional("modifiers", Value::Kind::Array));
    RAWFRAME_TRY_ASSIGN(binding.exactModifiers, kRecord.truth("exactModifiers", false));
    if ((modifiers != nullptr || binding.exactModifiers) && binding.device != DeviceClass::Keyboard) {
        return invalid(path, "only a keyboard binding has modifiers");
    }
    if (modifiers != nullptr) {
        for (std::size_t index = 0; index < modifiers->items().size(); ++index) {
            const Value& named = modifiers->items()[index];
            const auto kModifier =
                named.kind() == Value::Kind::String ? modifierNamed(*named.text()) : std::optional<Modifier>{};
            const auto kBit = kModifier ? static_cast<std::uint8_t>(*kModifier) : std::uint8_t{0};
            if (kBit == 0 || (binding.modifiers & kBit) != 0) {
                return invalid(indexed(kRecord.pathOf("modifiers"), index),
                               "a modifier is ctrl, shift, alt, or meta, each once");
            }
            binding.modifiers |= kBit;
        }
        if (modifiers->items().empty()) {
            return document::notCanonical(kRecord.pathOf("modifiers"), "a field at its default is omitted");
        }
    }

    // Processing: deadzones on axis controls, inversion on axes, scale.
    RAWFRAME_TRY_ASSIGN(const double kLower, kRecord.real("deadzoneLower", 0.2));
    RAWFRAME_TRY_ASSIGN(const double kUpper, kRecord.real("deadzoneUpper", 1.0));
    const bool kAxisControl = !kComposite && kShape != ControlShape::Digital;
    if ((kLower != 0.2 || kUpper != 1.0) && !kAxisControl) {
        return invalid(path, "only an axis control has a deadzone");
    }
    if (!(kLower >= 0 && kLower < kUpper && kUpper <= 1)) {
        return invalid(path, "a deadzone is 0 <= deadzoneLower < deadzoneUpper <= 1");
    }
    binding.deadzoneLower = static_cast<float>(kLower);
    binding.deadzoneUpper = static_cast<float>(kUpper);
    RAWFRAME_TRY_ASSIGN(binding.invertX, kRecord.truth("invertX", false));
    RAWFRAME_TRY_ASSIGN(binding.invertY, kRecord.truth("invertY", false));
    if (binding.invertX && kShape == ControlShape::Digital) {
        return invalid(kRecord.pathOf("invertX"), "only an axis is inverted");
    }
    if (binding.invertY && kShape != ControlShape::Axis2) {
        return invalid(kRecord.pathOf("invertY"), "only a two-axis value has a y to invert");
    }
    RAWFRAME_TRY_ASSIGN(const double kScale, kRecord.real("scale", 1.0));
    if (!std::isfinite(static_cast<float>(kScale))) {
        return invalid(kRecord.pathOf("scale"), "a scale is a finite number");
    }
    binding.scale = static_cast<float>(kScale);
    return binding;
}

namespace {

/// The shortest text that reads back to the same float.
document::Value floatNumber(float number) {
    std::array<char, 32> buffer{};
    const auto [kEnd, kError] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    static_cast<void>(kError);
    return document::Value::numberText(std::string{buffer.data(), kEnd});
}

} // namespace

document::Value bindingDocument(const Binding& binding, bool keyed) {
    Value record = Value::object();
    if (!keyed) {
        if (binding.slot != 0) {
            record.add("slot", Value::integer(binding.slot));
        }
        record.add("device", Value::string(std::string{nameOf(binding.device)}));
    }
    const auto kNamed = [](Control control) {
        return Value::string(std::string{nameOf(control)});
    };
    switch (binding.composite) {
    case Composite::None:
        record.add(binding.device == DeviceClass::Keyboard ? "physicalKey" : "control", kNamed(binding.controls[0]));
        break;
    case Composite::Pair: {
        Value pair = Value::object();
        pair.add("negative", kNamed(binding.controls[0]));
        pair.add("positive", kNamed(binding.controls[1]));
        record.add("pair", std::move(pair));
        break;
    }
    case Composite::Quad: {
        Value quad = Value::object();
        for (std::size_t part = 0; part < kQuadFields.size(); ++part) {
            quad.add(std::string{kQuadFields[part]}, kNamed(binding.controls[part]));
        }
        record.add("quad", std::move(quad));
        break;
    }
    }
    if (binding.composite == Composite::Quad && binding.mode == CompositeMode::Digital) {
        record.add("mode", Value::string("digital"));
    }
    if (binding.modifiers != 0) {
        Value modifiers = Value::array();
        for (const auto& [kName, kModifier] : {std::pair{"ctrl", Modifier::Ctrl},
                                               std::pair{"shift", Modifier::Shift},
                                               std::pair{"alt", Modifier::Alt},
                                               std::pair{"meta", Modifier::Meta}}) {
            if ((binding.modifiers & static_cast<std::uint8_t>(kModifier)) != 0) {
                modifiers.push(Value::string(kName));
            }
        }
        record.add("modifiers", std::move(modifiers));
    }
    if (binding.exactModifiers) {
        record.add("exactModifiers", Value::boolean(true));
    }
    if (binding.deadzoneLower != 0.2F) {
        record.add("deadzoneLower", floatNumber(binding.deadzoneLower));
    }
    if (binding.deadzoneUpper != 1.0F) {
        record.add("deadzoneUpper", floatNumber(binding.deadzoneUpper));
    }
    if (binding.invertX) {
        record.add("invertX", Value::boolean(true));
    }
    if (binding.invertY) {
        record.add("invertY", Value::boolean(true));
    }
    if (binding.scale != 1.0F) {
        record.add("scale", floatNumber(binding.scale));
    }
    return record;
}

namespace {

result::Result<Action> readAction(const Value& value, const std::string& path, const ActionSetLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kActionFields, path));
    Action action;
    RAWFRAME_TRY_ASSIGN(action.id, identityOf(kRecord, "actionId"));
    RAWFRAME_TRY_ASSIGN(action.name, nameOf(kRecord, "name", limits.maximumNameLength));
    RAWFRAME_TRY_ASSIGN(const std::string_view kType, kRecord.text("valueType"));
    if (kType == "bool") {
        action.type = ValueType::Bool;
    } else if (kType == "axis1d") {
        action.type = ValueType::Axis1D;
    } else if (kType == "axis2d") {
        action.type = ValueType::Axis2D;
    } else {
        return invalid(kRecord.pathOf("valueType"), "a value type is bool, axis1d, or axis2d");
    }
    for (const auto& [kField, kInto] : {std::pair{std::string_view{"displayName"}, &action.displayName},
                                        std::pair{std::string_view{"group"}, &action.group}}) {
        RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kText, kRecord.optionalText(kField));
        if (kText && (kText->empty() || kText->size() > limits.maximumDisplayLength)) {
            return invalid(kRecord.pathOf(kField), "display text is not empty and within its limit");
        }
        *kInto = std::string{kText.value_or("")};
    }
    RAWFRAME_TRY_ASSIGN(const double kPress, kRecord.real("pressThreshold", 0.5));
    RAWFRAME_TRY_ASSIGN(const double kRelease, kRecord.real("releaseThreshold", kPress));
    if (!(kRelease >= 0 && kRelease <= kPress && kPress > 0 && kPress <= 1)) {
        return invalid(path, "thresholds are 0 <= releaseThreshold <= pressThreshold <= 1, pressThreshold above 0");
    }
    action.pressThreshold = static_cast<float>(kPress);
    action.releaseThreshold = static_cast<float>(kRelease);
    RAWFRAME_TRY_ASSIGN(action.consume, kRecord.truth("consume", true));
    RAWFRAME_TRY_ASSIGN(const Value* bindings, kRecord.optional("bindings", Value::Kind::Array));
    if (bindings != nullptr) {
        if (bindings->items().empty()) {
            return document::notCanonical(kRecord.pathOf("bindings"), "a field at its default is omitted");
        }
        std::set<std::pair<DeviceClass, std::uint32_t>> slots;
        for (std::size_t index = 0; index < bindings->items().size(); ++index) {
            const std::string kPath = indexed(kRecord.pathOf("bindings"), index);
            RAWFRAME_TRY_ASSIGN(Binding binding, readBinding(bindings->items()[index], action, kPath, nullptr));
            if (!slots.emplace(binding.device, binding.slot).second) {
                return invalid(kPath, "a slot of a device class holds one binding");
            }
            if (std::ranges::count(slots, binding.device, &std::pair<DeviceClass, std::uint32_t>::first) >
                static_cast<std::ptrdiff_t>(limits.maximumSlotsPerDeviceClass)) {
                return invalid(kPath, "more binding slots for one device class than allowed");
            }
            action.bindings.push_back(binding);
        }
    }
    return action;
}

result::Result<Context>
readContext(const Value& value, const std::string& path, const ActionSet& set, const ActionSetLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(value, kContextFields, path));
    Context context;
    RAWFRAME_TRY_ASSIGN(context.id, identityOf(kRecord, "contextId"));
    RAWFRAME_TRY_ASSIGN(context.name, nameOf(kRecord, "name", limits.maximumNameLength));
    RAWFRAME_TRY_ASSIGN(context.priority, kRecord.integer("priority", 0));
    RAWFRAME_TRY_ASSIGN(const Value* actions, kRecord.required("actions", Value::Kind::Array));
    for (std::size_t index = 0; index < actions->items().size(); ++index) {
        const Value& named = actions->items()[index];
        const auto kAction =
            named.kind() == Value::Kind::String ? set.actionNamed(*named.text()) : std::optional<std::size_t>{};
        if (!kAction) {
            return invalid(indexed(kRecord.pathOf("actions"), index), "no action of that name");
        }
        if (std::ranges::contains(context.actions, *kAction)) {
            return invalid(indexed(kRecord.pathOf("actions"), index), "a context holds an action once");
        }
        context.actions.push_back(*kAction);
    }
    RAWFRAME_TRY_ASSIGN(context.textEditGated, kRecord.truth("textEditGated", true));
    return context;
}

} // namespace

std::optional<std::uint64_t> parseIdentity(std::string_view text) noexcept {
    if (text.size() != 16) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char kDigit : text) {
        value <<= 4U;
        if (kDigit >= '0' && kDigit <= '9') {
            value |= static_cast<std::uint64_t>(kDigit - '0');
        } else if (kDigit >= 'a' && kDigit <= 'f') {
            value |= static_cast<std::uint64_t>(kDigit - 'a' + 10);
        } else {
            return std::nullopt;
        }
    }
    return value;
}

std::optional<std::size_t> ActionSet::actionNamed(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (actions[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> ActionSet::actionWithId(std::uint64_t id) const noexcept {
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (actions[index].id == id) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> ActionSet::contextNamed(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        if (contexts[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

result::Result<ActionSet> readActionSet(std::string_view text, const ActionSetLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Value kRoot, document::parseCanonical(text));
    // The version first: a document of another version is not read further.
    const Value* version = kRoot.find("formatVersion");
    if (version == nullptr || version->integer() != 1) {
        return invalid("formatVersion", "the format version is 1");
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(kRoot, kDocumentFields, "$"));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    if (kKind != "input.actions") {
        return invalid(kRecord.pathOf("kind"), "the kind is input.actions");
    }

    ActionSet set;
    std::set<std::uint64_t> identities;
    RAWFRAME_TRY_ASSIGN(const Value* actions, kRecord.required("actions", Value::Kind::Array));
    if (actions->items().size() > limits.maximumActions) {
        return invalid(kRecord.pathOf("actions"), "more actions than allowed");
    }
    for (std::size_t index = 0; index < actions->items().size(); ++index) {
        const std::string kPath = indexed(kRecord.pathOf("actions"), index);
        RAWFRAME_TRY_ASSIGN(Action action, readAction(actions->items()[index], kPath, limits));
        if (!identities.insert(action.id).second) {
            return invalid(kPath + ".actionId", "an identity is used once in a document");
        }
        if (set.actionNamed(action.name)) {
            return invalid(kPath + ".name", "an action's name is used once");
        }
        set.actions.push_back(std::move(action));
    }

    RAWFRAME_TRY_ASSIGN(const Value* contexts, kRecord.optional("contexts", Value::Kind::Array));
    if (contexts != nullptr) {
        if (contexts->items().empty()) {
            return document::notCanonical(kRecord.pathOf("contexts"), "a field at its default is omitted");
        }
        if (contexts->items().size() > limits.maximumContexts) {
            return invalid(kRecord.pathOf("contexts"), "more contexts than allowed");
        }
        for (std::size_t index = 0; index < contexts->items().size(); ++index) {
            const std::string kPath = indexed(kRecord.pathOf("contexts"), index);
            RAWFRAME_TRY_ASSIGN(Context context, readContext(contexts->items()[index], kPath, set, limits));
            if (!identities.insert(context.id).second) {
                return invalid(kPath + ".contextId", "an identity is used once in a document");
            }
            if (set.contextNamed(context.name)) {
                return invalid(kPath + ".name", "a context's name is used once");
            }
            set.contexts.push_back(std::move(context));
        }
    }

    RAWFRAME_TRY_ASSIGN(const Value* reserved, kRecord.optional("reserved", Value::Kind::Array));
    if (reserved != nullptr) {
        if (reserved->items().empty()) {
            return document::notCanonical(kRecord.pathOf("reserved"), "a field at its default is omitted");
        }
        for (std::size_t index = 0; index < reserved->items().size(); ++index) {
            const std::string kPath = indexed(kRecord.pathOf("reserved"), index);
            RAWFRAME_TRY_ASSIGN(const Record kEntry, Record::of(reserved->items()[index], kReservedFields, kPath));
            RAWFRAME_TRY_ASSIGN(const std::string_view kDevice, kEntry.text("device"));
            const auto kClass = deviceClassNamed(kDevice);
            if (!kClass) {
                return invalid(kEntry.pathOf("device"), "a device class is keyboard, mouse, or gamepad");
            }
            RAWFRAME_TRY_ASSIGN(const std::optional<Control> kControl, singleControl(kEntry, *kClass));
            if (!kControl) {
                return invalid(kPath, "a reserved entry names one control");
            }
            if (std::ranges::contains(set.reserved, *kControl)) {
                return invalid(kPath, "a control is reserved once");
            }
            set.reserved.push_back(*kControl);
        }
    }
    return set;
}

} // namespace rawframe::input
