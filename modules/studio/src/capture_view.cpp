#include "rawframe/authoring_session/game.h"
#include "shell.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <vector>

namespace rawframe::studio {

namespace {

std::string textOf(const Value* value, std::string_view otherwise) {
    return value != nullptr && value->text() != nullptr ? *value->text() : std::string{otherwise};
}

/// The place a row's id names after its word ("step 12"), or none's.
std::size_t placeIn(std::string_view id, std::size_t from) {
    std::size_t place = std::numeric_limits<std::size_t>::max();
    const std::string_view kDigits = id.substr(std::min(from, id.size()));
    static_cast<void>(std::from_chars(kDigits.data(), kDigits.data() + kDigits.size(), place));
    return place;
}

std::string countOf(const Value* value) {
    return std::to_string(value != nullptr ? value->integer().value_or(0) : 0);
}

} // namespace

void ShellParticipant::showCapture() {
    // A capture is an answer as it was read, a drawn frame's (D842) or a
    // profile's (D844), kept as a file: read as it is.
    const auto kFile = authoring_session::readFile(sceneRoot_ / scene_);
    auto parsed = document::parse(kFile.value_or(std::string{}));
    held_.capture = parsed.has_value() ? std::move(*parsed) : Value{};
    const Value* kFigures = held_.capture.find("figures");
    const Value* kSteps = held_.capture.find("steps");
    const auto kAdd = [this](const std::string& label, const std::string& id) {
        auto added = row(entitiesColumn_, label, kText, kRow, true);
        if (!added.has_value()) {
            return false;
        }
        entityRows_.push_back(*added);
        entities_.push_back(id);
        names_.push_back(label);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
        return true;
    };
    // A profile's capture (D844): its ticks, then each system.
    if (const Value* kTicks = held_.capture.find("ticksUs");
        kTicks != nullptr && kTicks->kind() == Value::Kind::Array) {
        if (!kAdd("ticks (" + std::to_string(kTicks->items().size()) + ")", "ticks")) {
            return;
        }
        const Value* kSystems = held_.capture.find("systems");
        const std::span<const Value> kEach = kSystems != nullptr && kSystems->kind() == Value::Kind::Array
                                                 ? kSystems->items()
                                                 : std::span<const Value>{};
        std::size_t shown = 0;
        for (; shown < kEach.size() && entityRows_.size() < EditorsHeld::kMostRows; ++shown) {
            if (!kAdd(textOf(kEach[shown].find("system"), "?"), "system " + std::to_string(shown))) {
                return;
            }
        }
        emitter_.log(diagnostics::Severity::Info,
                     kCaptureShown,
                     "a profile's capture shown, read only",
                     {diagnostics::field("capture", scene_),
                      diagnostics::field("ticks", kTicks->items().size()),
                      diagnostics::field("systems", shown)});
        return;
    }
    if (kFigures == nullptr || kFigures->kind() != Value::Kind::Object) {
        static_cast<void>(kAdd("not a capture Studio reads", "none"));
        return;
    }
    if (!kAdd("figures (" + std::to_string(kFigures->items().size()) + ")", "figures")) {
        return;
    }
    // Each step a row, as it was drawn, the first that fit.
    const std::span<const Value> kAll =
        kSteps != nullptr && kSteps->kind() == Value::Kind::Array ? kSteps->items() : std::span<const Value>{};
    std::size_t shown = 0;
    for (; shown < kAll.size() && entityRows_.size() < EditorsHeld::kMostRows; ++shown) {
        const Value& kStep = kAll[shown];
        if (!kAdd(textOf(kStep.find("pass"), "?") + " (" + countOf(kStep.find("instances")) + " drawn)",
                  "step " + std::to_string(shown))) {
            return;
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kCaptureShown,
                 "a frame's capture shown, read only",
                 {diagnostics::field("capture", scene_),
                  diagnostics::field("figures", kFigures->items().size()),
                  diagnostics::field("steps", kAll.size()),
                  diagnostics::field("shown", shown)});
}

void ShellParticipant::showCaptureEntry(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const auto kLine = [this](const std::string& words, std::uint32_t color) {
        if (auto made = row(componentsColumn_, words, color, kPanel); made.has_value()) {
            componentRows_.push_back(*made);
        }
    };
    if (entity_ == "figures") {
        const Value* kFigures = held_.capture.find("figures");
        kLine("figures of the frame, " + countOf(held_.capture.find("frames")) + " frames into the preview", kQuiet);
        for (std::size_t each = 0; kFigures != nullptr && each < kFigures->names().size(); ++each) {
            kLine(kFigures->names()[each] + ": " + countOf(&kFigures->items()[each]), kText);
        }
    } else if (entity_ == "ticks") {
        // The ticks' spread, as the Profiler says it.
        const Value* kTicks = held_.capture.find("ticksUs");
        std::vector<std::int64_t> ticks;
        for (const Value& kTick : kTicks != nullptr ? kTicks->items() : std::span<const Value>{}) {
            ticks.push_back(kTick.integer().value_or(0));
        }
        std::ranges::sort(ticks);
        kLine(std::to_string(ticks.size()) + " ticks, each " + countOf(held_.capture.find("tickUs")) + " us", kQuiet);
        if (!ticks.empty()) {
            kLine("half within " + std::to_string(ticks[ticks.size() / 2]) + " us", kText);
            kLine("longest " + std::to_string(ticks.back()) + " us", kText);
        }
    } else if (entity_.starts_with("system ")) {
        const Value* kSystems = held_.capture.find("systems");
        const Value* kTicks = held_.capture.find("ticksUs");
        const std::size_t kAt = placeIn(entity_, 7);
        if (kSystems == nullptr || kAt >= kSystems->items().size()) {
            return;
        }
        const Value& kSystem = kSystems->items()[kAt];
        const std::int64_t kTotal =
            kSystem.find("totalUs") != nullptr ? kSystem.find("totalUs")->integer().value_or(0) : 0;
        const std::size_t kCount = kTicks != nullptr ? kTicks->items().size() : 0;
        kLine(textOf(kSystem.find("system"), "?"), kQuiet);
        kLine("total " + std::to_string(kTotal) + " us over " + std::to_string(kCount) + " ticks", kText);
        kLine("mean " + std::to_string(kCount > 0 ? kTotal / static_cast<std::int64_t>(kCount) : 0) + " us", kText);
        kLine("longest " + countOf(kSystem.find("mostUs")) + " us", kText);
    } else if (entity_.starts_with("step ")) {
        const Value* kSteps = held_.capture.find("steps");
        const std::size_t kAt = placeIn(entity_, 5);
        if (kSteps == nullptr || kAt >= kSteps->items().size()) {
            return;
        }
        const Value& kStep = kSteps->items()[kAt];
        kLine("step " + std::to_string(kAt + 1) + " of " + std::to_string(kSteps->items().size()), kQuiet);
        kLine("pass: " + textOf(kStep.find("pass"), "?"), kText);
        kLine("mesh: " + textOf(kStep.find("mesh"), "none"), kText);
        kLine("material: " + countOf(kStep.find("material")), kText);
        kLine("instances: " + countOf(kStep.find("instances")), kText);
    }
    ++components_;
}

} // namespace rawframe::studio
