#include "records.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

// The scene's view as Studio edits it: typed into the view's fields, and
// turned into a 2D view or out of one (D471, D793).

namespace rawframe::studio {

namespace {

/// The numbers `text` holds apart by spaces or commas, if every part is one.
std::optional<std::vector<double>> numbersOf(std::string_view text) {
    std::vector<double> numbers;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t kEnd = text.find_first_of(" ,", at);
        const std::string_view kPart = text.substr(at, kEnd == std::string_view::npos ? text.size() - at : kEnd - at);
        if (!kPart.empty()) {
            const auto kParsed = document::parse(kPart);
            if (!kParsed.has_value() || !kParsed->real().has_value()) {
                return std::nullopt;
            }
            numbers.push_back(*kParsed->real());
        }
        if (kEnd == std::string_view::npos) {
            break;
        }
        at = kEnd + 1;
    }
    return numbers;
}

Value point(double x, double y, double z) {
    Value made = Value::array();
    made.push(Value::real(x));
    made.push(Value::real(y));
    made.push(Value::real(z));
    return made;
}

} // namespace

std::optional<Value> viewWith(const std::optional<Value>& current, std::string_view part, std::string_view text) {
    const auto kNumbers = numbersOf(text);
    const std::size_t kWanted = part == "fieldOfView" ? 1 : 3;
    if (!kNumbers.has_value() || kNumbers->size() != kWanted ||
        (part != "eye" && part != "target" && part != "fieldOfView")) {
        return std::nullopt;
    }
    Value made = Value::object();
    for (const std::string_view kName : {"eye", "target", "fieldOfView"}) {
        const Value* held = current.has_value() && !current->isNull() ? current->find(kName) : nullptr;
        if (kName == part) {
            made.add(std::string{kName},
                     kWanted == 1 ? Value::real((*kNumbers)[0])
                                  : point((*kNumbers)[0], (*kNumbers)[1], (*kNumbers)[2]));
        } else if (held != nullptr) {
            made.add(std::string{kName}, *held);
        } else if (kName == "eye") {
            made.add("eye", point(0, 10, 10));
        } else if (kName == "target") {
            made.add("target", point(0, 0, 0));
        } else {
            made.add("fieldOfView", Value::real(60));
        }
    }
    return made;
}

std::string viewText(const std::optional<Value>& view, std::string_view part) {
    const Value* held = view.has_value() && !view->isNull() ? view->find(part) : nullptr;
    if (held == nullptr) {
        return {};
    }
    if (held->kind() != Value::Kind::Array) {
        return document::writeCompact(*held);
    }
    std::string text;
    for (const Value& each : held->items()) {
        text += (text.empty() ? "" : " ") + document::writeCompact(each);
    }
    return text;
}

std::optional<Value> flatView(const std::optional<Value>& view, bool flat) {
    // A scene not yet looked at is looked at as `viewWith` starts one.
    const std::optional<Value> kView =
        view.has_value() && !view->isNull() ? view : viewWith(std::nullopt, "fieldOfView", "60");
    std::array<double, 3> eye{};
    std::array<double, 3> target{};
    for (const auto& [kName, kInto] : {std::pair{"eye", &eye}, std::pair{"target", &target}}) {
        const Value* held = kView->find(kName);
        if (held == nullptr || held->kind() != Value::Kind::Array || held->items().size() != 3) {
            return std::nullopt;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const std::optional<double> kReal = held->items()[axis].real();
            if (!kReal.has_value()) {
                return std::nullopt;
            }
            (*kInto)[axis] = *kReal;
        }
    }
    const double kFar = std::max(std::hypot(eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]), 3.0);
    Value made = Value::object();
    if (flat) {
        made.add("eye", point(target[0], target[1], target[2] + kFar));
    } else {
        made.add("eye", point(target[0], target[1] + (kFar * 0.6), target[2] + (kFar * 0.8)));
    }
    made.add("target", point(target[0], target[1], target[2]));
    const Value* degrees = kView->find("fieldOfView");
    made.add("fieldOfView", degrees != nullptr ? *degrees : Value::real(60));
    return made;
}

} // namespace rawframe::studio
