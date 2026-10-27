#include "rawframe/scene/scene.h"
#include "rawframe/schema/stable_id.h"
#include "shell.h"

#include <algorithm>
#include <map>

namespace rawframe::studio {

namespace {

std::string streamingWord(scene::Streaming streaming) {
    return streaming == scene::Streaming::Atomic       ? "atomic"
           : streaming == scene::Streaming::Persistent ? "persistent"
                                                       : "each";
}

/// The resource identity a sidecar's text gives, as 32 hex digits; empty
/// for none.
std::string resourceOf(std::string_view sidecar) {
    constexpr std::string_view kMember = "\"resourceId\": \"";
    const std::size_t kAt = sidecar.find(kMember);
    if (kAt == std::string_view::npos || kAt + kMember.size() + 32 > sidecar.size()) {
        return {};
    }
    return std::string{sidecar.substr(kAt + kMember.size(), 32)};
}

} // namespace

void ShellParticipant::showWorld() {
    clear(worldRows_);
    districtPresses_.clear();
    if (scene_.empty()) {
        return;
    }
    // The scene as its file holds it, written by every change the session
    // makes, and the game's scenes by their resource identities, to name
    // what each instance is of.
    const std::filesystem::path kFile = sceneRoot_ / scene_;
    const auto kScene = scene::readScene(authoring_session::readFile(kFile).value_or(""));
    if (!kScene.has_value() || kScene->instances.empty()) {
        if (auto none = row(
                worldColumn_, "no districts: a district is a scene instanced here, streamed as one", kQuiet, kPanel);
            none.has_value()) {
            worldRows_.push_back(*none);
        }
        return;
    }
    std::map<std::string, std::string> named;
    for (const std::string& kOther : scenes_) {
        if (kOther.ends_with(".scene")) {
            const std::string kResource =
                resourceOf(authoring_session::readFile(sceneRoot_ / (kOther + ".rfmeta")).value_or(""));
            if (!kResource.empty()) {
                named.emplace(kResource, kOther);
            }
        }
    }
    std::size_t districts = 0;
    for (const scene::SceneInstance& kInstance : kScene->instances) {
        districts += kInstance.streaming == scene::Streaming::Each ? 0 : 1;
    }
    // As the design's World screen: the scene and how many it
    // places, then each instance a row of its mode's color, its name, its
    // mode as a chip pressed for the next, and its entities; the row
    // pressed chooses it.
    if (auto head =
            box(worldColumn_,
                ui::Layout{.direction = ui::Direction::Column, .gap = 2, .shrink = 0, .padding = {12, 12, 10, 8}},
                0);
        head.has_value()) {
        worldRows_.push_back(*head);
        if (auto title = box(*head, ui::Layout{.shrink = 0}, 0); title.has_value()) {
            static_cast<void>(strongWords(*title, scene_, look::kInk, look::kBodySize));
        }
        if (auto counted = box(*head, ui::Layout{.shrink = 0}, 0); counted.has_value()) {
            static_cast<void>(words(*counted,
                                    std::to_string(kScene->instances.size()) + " placed scenes · " +
                                        std::to_string(districts) + " streamed whole",
                                    look::kLabelInk,
                                    look::kLabelSize));
        }
    }
    for (const scene::SceneInstance& kInstance : kScene->instances) {
        std::array<char, base::kBits128HexDigits> digits{};
        base::formatBits128Hex(kInstance.scene, digits);
        const auto kName = named.find(std::string{digits.data(), digits.size()});
        const std::string kStreaming = streamingWord(kInstance.streaming);
        const std::array<char, schema::kStableIdTextLength> kEntity =
            schema::formatStableIdText(kInstance.entities.front().instance);
        const std::string kId{kEntity.data(), kEntity.size()};
        const std::string kSource = kName != named.end() ? kName->second : std::string{digits.data(), 8};
        const auto kListedAt = std::ranges::find(entities_, kId);
        const std::string kInstanceName =
            kListedAt != entities_.end() && !names_[static_cast<std::size_t>(kListedAt - entities_.begin())].empty()
                ? names_[static_cast<std::size_t>(kListedAt - entities_.begin())]
                : kSource;
        // Each by itself streams near a player, atomic loads all at once,
        // persistent is always there: green, orange, and blue.
        const std::uint32_t kInk = kInstance.streaming == scene::Streaming::Atomic       ? 0xE8A33DFFU
                                   : kInstance.streaming == scene::Streaming::Persistent ? 0x5B9BFFFFU
                                                                                         : 0x3DBB6BFFU;
        auto line = box(worldColumn_,
                        ui::Layout{.height = ui::pixels(28),
                                   .alignItems = ui::Align::Center,
                                   .gap = 8,
                                   .shrink = 0,
                                   .padding = {12, 10, 0, 0},
                                   .margin = {6, 6, 1, 1}},
                        0);
        if (!line.has_value()) {
            return;
        }
        worldRows_.push_back(*line);
        static_cast<void>(tree_->setLook(
            *line, ui::Look{.fill = std::ranges::contains(selected_, kId) ? kChosen : kRow, .radius = 4}));
        static_cast<void>(tree_->setRole(*line, ui::Role::Button));
        static_cast<void>(tree_->setName(*line, kInstanceName + " " + kSource));
        if (auto dot = box(*line, ui::Layout{.width = ui::pixels(9), .height = ui::pixels(9), .shrink = 0}, 0);
            dot.has_value()) {
            static_cast<void>(tree_->setLook(*dot, ui::Look{.fill = kInk, .radius = 2}));
            static_cast<void>(tree_->setInteraction(*dot, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        if (auto said = box(*line, ui::Layout{.alignItems = ui::Align::Center, .gap = 6, .grow = 1}, 0);
            said.has_value()) {
            static_cast<void>(tree_->setInteraction(*said, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            if (auto shown = box(*said, ui::Layout{.shrink = 0}, 0); shown.has_value()) {
                static_cast<void>(words(*shown, kInstanceName, look::kInk, look::kBodySize));
            }
            if (auto from = box(*said, ui::Layout{.shrink = 1}, 0); from.has_value() && kInstanceName != kSource) {
                static_cast<void>(words(*from, kSource, look::kFaintInk, look::kLabelSize));
            }
        }
        const std::string kMode = kStreaming == "each" ? std::string{"stream"} : kStreaming;
        if (auto chip = box(*line, ui::Layout{.shrink = 0, .padding = {7, 7, 1, 2}}, 0); chip.has_value()) {
            static_cast<void>(tree_->setLook(*chip, ui::Look{.fill = (kInk & 0xFFFFFF00U) | 0x30U, .radius = 9}));
            static_cast<void>(monoWords(*chip, kMode, kInk, look::kSmallSize));
            static_cast<void>(tree_->setRole(*chip, ui::Role::Button));
            static_cast<void>(tree_->setName(*chip, kInstanceName + " mode " + kMode));
            districtPresses_.push_back({*chip, DistrictPress{.entity = kId, .streaming = kStreaming}});
        }
        if (auto count = box(*line, ui::Layout{.width = ui::pixels(28), .shrink = 0}, 0); count.has_value()) {
            static_cast<void>(monoWords(*count, std::to_string(kInstance.entities.size()), look::kFaintInk, 12));
            static_cast<void>(tree_->setInteraction(*count, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        districtPresses_.push_back({*line, DistrictPress{.entity = kId, .streaming = {}}});
    }
    // What each mode means, as the design's legend.
    for (const auto& [kWord, kSays] :
         {std::pair<std::string_view, std::string_view>{"stream", "each entity sent near a player"},
          {"atomic", "all its entities sent together, near a player"},
          {"persistent", "always sent to every player"}}) {
        if (auto legend = box(worldColumn_, ui::Layout{.gap = 6, .shrink = 0, .padding = {14, 12, 2, 2}}, 0);
            legend.has_value()) {
            worldRows_.push_back(*legend);
            if (auto word = box(*legend, ui::Layout{.width = ui::pixels(74), .shrink = 0}, 0); word.has_value()) {
                static_cast<void>(monoWords(*word, kWord, look::kLabelInk, look::kSmallSize));
            }
            if (auto said = box(*legend, ui::Layout{.shrink = 1}, 0); said.has_value()) {
                static_cast<void>(words(*said, kSays, look::kFaintInk, look::kSmallSize));
            }
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kWorldShown,
                 "a scene's instances as districts, each streamed as it says",
                 {diagnostics::field("scene", scene_),
                  diagnostics::field("instances", kScene->instances.size()),
                  diagnostics::field("districts", districts)});
}

bool ShellParticipant::pressWorld(ui::Node node) {
    const auto kPress = std::ranges::find(districtPresses_, node, &std::pair<ui::Node, DistrictPress>::first);
    if (kPress == districtPresses_.end()) {
        return false;
    }
    // A row chooses its instance, in the Hierarchy and the Inspector.
    if (kPress->second.streaming.empty()) {
        if (const auto kAt = std::ranges::find(entities_, kPress->second.entity); kAt != entities_.end()) {
            chooseEntity(static_cast<std::size_t>(kAt - entities_.begin()));
        }
        showWorld();
        return true;
    }
    // Its chip: each entity by itself, then all together, then all always,
    // and round again, as the session's scene.stream_instance.
    const std::string kNow = kPress->second.streaming;
    const std::string kNext = kNow == "each" ? "atomic" : kNow == "atomic" ? "persistent" : "each";
    Value operation = Value::object();
    operation.add("operation", Value::string("scene.stream_instance"));
    operation.add("entity", Value::string(kPress->second.entity));
    operation.add("streaming", Value::string(kNext));
    commit(std::move(operation), "district streamed " + kNext, std::nullopt);
    showWorld();
    return true;
}

} // namespace rawframe::studio
