#include "rawframe/world_tooling/window_input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace rawframe::world_tooling {

namespace {

using document::Value;

constexpr std::array<std::pair<std::string_view, WindowInput::Kind>, 8> kTypes = {{
    {"move", WindowInput::Kind::Move},
    {"press", WindowInput::Kind::Press},
    {"release", WindowInput::Kind::Release},
    {"wheel", WindowInput::Kind::Wheel},
    {"key_down", WindowInput::Kind::KeyDown},
    {"key_up", WindowInput::Kind::KeyUp},
    {"text", WindowInput::Kind::Text},
    {"leave", WindowInput::Kind::Leave},
}};

/// A picture is at most this many logical pixels a side.
constexpr double kFarthest = 16384;

std::optional<float> numberOf(const Value& event, std::string_view name) {
    const Value* held = event.find(name);
    const std::optional<double> kReal = held != nullptr ? held->real() : std::nullopt;
    if (!kReal.has_value() || !std::isfinite(*kReal) || std::abs(*kReal) > kFarthest) {
        return std::nullopt;
    }
    return static_cast<float>(*kReal);
}

std::optional<std::int64_t> wholeOf(const Value& event, std::string_view name, std::int64_t most) {
    const Value* held = event.find(name);
    const std::optional<std::int64_t> kWhole = held != nullptr ? held->integer() : std::nullopt;
    if (!kWhole.has_value() || *kWhole < 0 || *kWhole > most) {
        return std::nullopt;
    }
    return kWhole;
}

/// One event, or none for one that does not read.
std::optional<WindowInput> windowInputOf(const Value& event) {
    const Value* type = event.find("type");
    if (type == nullptr || type->text() == nullptr) {
        return std::nullopt;
    }
    const auto kType = std::ranges::find(
        kTypes, std::string_view{*type->text()}, &std::pair<std::string_view, WindowInput::Kind>::first);
    if (kType == kTypes.end()) {
        return std::nullopt;
    }
    WindowInput made{.kind = kType->second};
    // The modifiers held, where any are.
    if (event.find("modifiers") != nullptr) {
        const auto kModifiers = wholeOf(event, "modifiers", 0xFFFF);
        if (!kModifiers.has_value()) {
            return std::nullopt;
        }
        made.modifiers = static_cast<std::uint16_t>(*kModifiers);
    }
    switch (made.kind) {
    case WindowInput::Kind::Move:
    case WindowInput::Kind::Press:
    case WindowInput::Kind::Release: {
        const auto kX = numberOf(event, "x");
        const auto kY = numberOf(event, "y");
        if (!kX.has_value() || !kY.has_value()) {
            return std::nullopt;
        }
        made.x = *kX;
        made.y = *kY;
        if (made.kind != WindowInput::Kind::Move) {
            const auto kButton = wholeOf(event, "button", 2);
            if (!kButton.has_value()) {
                return std::nullopt;
            }
            made.button = static_cast<std::uint8_t>(*kButton);
        }
        return made;
    }
    case WindowInput::Kind::Wheel: {
        const auto kX = numberOf(event, "deltaX");
        const auto kY = numberOf(event, "deltaY");
        if (!kX.has_value() || !kY.has_value()) {
            return std::nullopt;
        }
        made.deltaX = *kX;
        made.deltaY = *kY;
        return made;
    }
    case WindowInput::Kind::KeyDown:
    case WindowInput::Kind::KeyUp: {
        const auto kUsage = wholeOf(event, "usage", 0xFFFF);
        const Value* repeat = event.find("repeat");
        if (!kUsage.has_value() || (repeat != nullptr && !repeat->truth().has_value())) {
            return std::nullopt;
        }
        made.usage = static_cast<std::uint16_t>(*kUsage);
        made.repeat = repeat != nullptr && *repeat->truth();
        return made;
    }
    case WindowInput::Kind::Text: {
        // Parsed JSON's text is UTF-8 already; only its length is bounded.
        const Value* text = event.find("text");
        if (text == nullptr || text->text() == nullptr || text->text()->empty() || text->text()->size() > kMostText) {
            return std::nullopt;
        }
        made.text = *text->text();
        return made;
    }
    case WindowInput::Kind::Leave:
        return made;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::vector<WindowInput>> windowInputsOf(const Value& events) {
    if (events.kind() != Value::Kind::Array || events.items().size() > kMostWindowInputs) {
        return std::nullopt;
    }
    std::vector<WindowInput> made;
    for (const Value& each : events.items()) {
        if (each.kind() != Value::Kind::Object) {
            return std::nullopt;
        }
        std::optional<WindowInput> event = windowInputOf(each);
        if (!event.has_value()) {
            return std::nullopt;
        }
        made.push_back(std::move(*event));
    }
    return made;
}

Value windowInputsValue(const std::vector<WindowInput>& events) {
    Value made = Value::array();
    for (const WindowInput& kEvent : events) {
        Value each = Value::object();
        const auto kType =
            std::ranges::find(kTypes, kEvent.kind, &std::pair<std::string_view, WindowInput::Kind>::second);
        each.add("type", Value::string(std::string{kType->first}));
        if (kEvent.modifiers != 0) {
            each.add("modifiers", Value::integer(kEvent.modifiers));
        }
        switch (kEvent.kind) {
        case WindowInput::Kind::Move:
        case WindowInput::Kind::Press:
        case WindowInput::Kind::Release:
            each.add("x", Value::real(kEvent.x));
            each.add("y", Value::real(kEvent.y));
            if (kEvent.kind != WindowInput::Kind::Move) {
                each.add("button", Value::integer(kEvent.button));
            }
            break;
        case WindowInput::Kind::Wheel:
            each.add("deltaX", Value::real(kEvent.deltaX));
            each.add("deltaY", Value::real(kEvent.deltaY));
            break;
        case WindowInput::Kind::KeyDown:
        case WindowInput::Kind::KeyUp:
            each.add("usage", Value::integer(kEvent.usage));
            if (kEvent.repeat) {
                each.add("repeat", Value::boolean(true));
            }
            break;
        case WindowInput::Kind::Text:
            each.add("text", Value::string(kEvent.text));
            break;
        case WindowInput::Kind::Leave:
            break;
        }
        made.push(std::move(each));
    }
    return made;
}

} // namespace rawframe::world_tooling
