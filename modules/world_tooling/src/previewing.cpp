#include "previewing.h"

#include "replies.h"

#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace rawframe::world_tooling {

using document::Value;

std::optional<std::string>
answerPreview(std::string_view kind, const Value& record, const Value& id, const ToolingSettings& settings) {
    if (kind == "tooling.look") {
        if (!settings.grants.view) {
            return errorLine(id, ToolingError::NotGranted, "looking needs the view grant");
        }
        if (settings.previewer == nullptr) {
            return errorLine(id, ToolingError::NotFound, "this Runtime shows no preview");
        }
        std::string refusal;
        const std::optional<std::optional<Look>> kLook = lookOf(record, refusal);
        if (!kLook.has_value()) {
            return errorLine(id, ToolingError::Malformed, refusal);
        }
        // And the looks the window is split to show beside it (D797):
        // at most three, each a view as `view` is.
        std::vector<Look> others;
        if (const Value* kOthers = record.find("others"); kOthers != nullptr) {
            bool sound = kOthers->kind() == Value::Kind::Array && kOthers->items().size() <= 3;
            for (std::size_t at = 0; sound && at < kOthers->items().size(); ++at) {
                Value one = Value::object();
                one.add("view", kOthers->items()[at]);
                std::string ignored;
                const std::optional<std::optional<Look>> kOther = lookOf(one, ignored);
                sound = kOther.has_value() && kOther->has_value();
                if (sound) {
                    others.push_back(**kOther);
                }
            }
            if (!sound) {
                return errorLine(id, ToolingError::Malformed, "others are at most three views");
            }
        }
        if (!settings.previewer->look(*kLook)) {
            return errorLine(id, ToolingError::Malformed, "a view's eye is at its target");
        }
        const bool kSplit = settings.previewer->split(others);
        Value made = Value::object();
        made.add("kind", Value::string("tooling.looking"));
        made.add("previewing", Value::boolean(kLook->has_value()));
        made.add("split", Value::integer(kSplit ? static_cast<std::int64_t>(others.size()) : 0));
        return replyLine(id, "answer", std::move(made));
    }
    if (kind == "tooling.mark") {
        // A point the preview marks, or none (D464): the view's.
        if (!settings.grants.view) {
            return errorLine(id, ToolingError::NotGranted, "marking needs the view grant");
        }
        const Value* kAt = record.find("at");
        const std::optional<std::array<double, 3>> kPoint = pointOf(kAt);
        if (kAt == nullptr || (!kAt->isNull() && !kPoint.has_value())) {
            return errorLine(id, ToolingError::Malformed, "at is a point of three finite numbers, or null");
        }
        Value made = Value::object();
        made.add("kind", Value::string("tooling.marked"));
        // The part drawn lit, by name; none unless named (D468).
        const Value* kLit = record.find("lit");
        constexpr std::array<std::string_view, 5> kParts = {"", "x", "y", "z", "ring"};
        std::uint8_t lit = 0;
        if (kLit != nullptr && !kLit->isNull()) {
            const auto kNamed = kLit->text() != nullptr ? std::ranges::find(kParts, *kLit->text()) : kParts.end();
            if (kNamed == kParts.end() || kNamed == kParts.begin()) {
                return errorLine(id, ToolingError::Malformed, "lit is x, y, z, ring, or null");
            }
            lit = static_cast<std::uint8_t>(kNamed - kParts.begin());
        }
        made.add("marked", Value::boolean(settings.previewer != nullptr && settings.previewer->mark(kPoint, lit)));
        return replyLine(id, "answer", std::move(made));
    }
    if (kind == "tooling.hide") {
        // The layers the preview leaves out (D791): the view's.
        if (!settings.grants.view) {
            return errorLine(id, ToolingError::NotGranted, "hiding needs the view grant");
        }
        const Value* kLayers = record.find("layers");
        std::vector<std::uint32_t> layers;
        bool sound = kLayers != nullptr && kLayers->kind() == Value::Kind::Array && kLayers->items().size() <= 64;
        for (std::size_t at = 0; sound && at < kLayers->items().size(); ++at) {
            const std::optional<std::int64_t> kLayer = kLayers->items()[at].integer();
            sound = kLayer.has_value() && *kLayer > 0 && *kLayer <= std::numeric_limits<std::uint32_t>::max();
            if (sound) {
                layers.push_back(static_cast<std::uint32_t>(*kLayer));
            }
        }
        if (!sound) {
            return errorLine(id, ToolingError::Malformed, "layers are at most 64 layer numbers, each above nought");
        }
        Value made = Value::object();
        made.add("kind", Value::string("tooling.hidden"));
        made.add("hidden", Value::boolean(settings.previewer != nullptr && settings.previewer->hide(layers)));
        return replyLine(id, "answer", std::move(made));
    }
    if (kind == "tooling.overlays") {
        // What the preview draws over its scene (D798): the view's.
        // The names are the renderer's; this only bounds them.
        if (!settings.grants.view) {
            return errorLine(id, ToolingError::NotGranted, "overlays need the view grant");
        }
        const Value* kOverlays = record.find("overlays");
        std::vector<std::string> names;
        bool sound = kOverlays != nullptr && kOverlays->kind() == Value::Kind::Array && kOverlays->items().size() <= 8;
        for (std::size_t at = 0; sound && at < kOverlays->items().size(); ++at) {
            const Value& kItem = kOverlays->items()[at];
            const std::string* kName = kItem.kind() == Value::Kind::String ? kItem.text() : nullptr;
            sound = kName != nullptr && !kName->empty() && kName->size() <= 32;
            if (sound) {
                names.push_back(*kName);
            }
        }
        if (!sound) {
            return errorLine(id, ToolingError::Malformed, "overlays are at most 8 names");
        }
        Value made = Value::object();
        made.add("kind", Value::string("tooling.overlays"));
        made.add("shown", Value::boolean(settings.previewer != nullptr && settings.previewer->overlay(names)));
        return replyLine(id, "answer", std::move(made));
    }
    if (kind == "tooling.view_mode") {
        // How the preview is drawn (D794): the view's. The names are
        // the renderer's; this only bounds the text.
        if (!settings.grants.view) {
            return errorLine(id, ToolingError::NotGranted, "a view mode needs the view grant");
        }
        const Value* kMode = record.find("mode");
        if (kMode == nullptr || kMode->text() == nullptr || kMode->text()->empty() || kMode->text()->size() > 32) {
            return errorLine(id, ToolingError::Malformed, "a view mode is a name");
        }
        Value made = Value::object();
        made.add("kind", Value::string("tooling.view_mode"));
        made.add("shown",
                 Value::boolean(settings.previewer != nullptr && settings.previewer->viewMode(*kMode->text())));
        return replyLine(id, "answer", std::move(made));
    }
    return std::nullopt;
}

} // namespace rawframe::world_tooling
