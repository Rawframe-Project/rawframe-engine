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
        std::array<char, base::kBits128HexDigits> digits{};
        base::formatBits128Hex(kInstance.scene, digits);
        const auto kName = named.find(std::string{digits.data(), digits.size()});
        const std::string kStreaming = streamingWord(kInstance.streaming);
        const std::array<char, schema::kStableIdTextLength> kEntity =
            schema::formatStableIdText(kInstance.entities.front().instance);
        auto line = box(worldColumn_, ui::Layout{.height = ui::pixels(28), .shrink = 0}, 0);
        auto pressed = line.has_value() ? box(*line, ui::Layout{.grow = 1, .padding = {8, 4, 8, 4}}, kRow)
                                        : std::unexpected<result::Error>{line.error().clone()};
        if (!pressed.has_value()) {
            return;
        }
        worldRows_.push_back(*line);
        static_cast<void>(pressWords(*pressed,
                                     (kName != named.end() ? kName->second : std::string{digits.data(), 8}) + "  " +
                                         std::to_string(kInstance.entities.size()) + "  " + kStreaming,
                                     kInstance.streaming == scene::Streaming::Each ? kQuiet : kText,
                                     14));
        districtPresses_.push_back(
            {*pressed, DistrictPress{.entity = std::string{kEntity.data(), kEntity.size()}, .streaming = kStreaming}});
        districts += kInstance.streaming == scene::Streaming::Each ? 0 : 1;
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
    // Each entity by itself, then all together, then all always, and
    // round again, as the session's scene.stream_instance.
    const std::string& kNow = kPress->second.streaming;
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
