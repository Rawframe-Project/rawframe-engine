#include "shell.h"

#include <algorithm>
#include <cstdio>

namespace rawframe::studio {

namespace {

constexpr std::size_t kStepsShown = 12;

/// The figure `name` of a drawn frame's answer, nought where it is not.
std::int64_t figureOf(const Value& figures, std::string_view name) {
    const Value* kFigure = figures.find(name);
    return kFigure != nullptr ? kFigure->integer().value_or(0) : 0;
}

} // namespace

result::Status ShellParticipant::makeFrame() {
    RAWFRAME_TRY_ASSIGN(held_.frameColumn, pane("Frame"));
    RAWFRAME_TRY_ASSIGN(const ui::Node kHead,
                        box(held_.frameColumn, ui::Layout{.gap = 6, .shrink = 0, .padding = {8, 8, 6, 0}}, 0));
    RAWFRAME_TRY_ASSIGN(held_.timelineTab, button(kHead, "Timeline"));
    RAWFRAME_TRY_ASSIGN(held_.frameCapture, button(kHead, "Capture frame"));
    if (auto made = row(held_.frameColumn, "play the game to see what its preview's frames hold", kQuiet, kPanel);
        made.has_value()) {
        held_.frameRows.push_back(*made);
    }
    return {};
}

bool ShellParticipant::pressCapture(ui::Node node) {
    if (node != held_.frameCapture && node != held_.profilerCapture) {
        return false;
    }
    if (!playing_.has_value() || previewedName().empty()) {
        say("play the game to capture it");
        return true;
    }
    // Kept under the game's directory, a capture a tool opens as Studio
    // does: the frame's (D842) or the profile's (D844).
    std::string path;
    if (node == held_.frameCapture) {
        path = "captures/frame-" + std::to_string(held_.frameCaptures + 1) + ".rfframe";
        const std::uint64_t kBefore = held_.frameCaptures;
        readFrame(-1, path);
        if (held_.frameCaptures == kBefore) {
            return true;
        }
    } else {
        path = "captures/profile-" + std::to_string(held_.profilerCaptures + 1) + ".rfprofile";
        const std::string kReply = ask(profileRecord(next(), previewedName(), path));
        if (kReply.find("\"capture\":") == std::string::npos) {
            say("the profile was not kept: " + answeredOf(kReply).message);
            return true;
        }
        ++held_.profilerCaptures;
        say("kept the profile in " + path);
    }
    // Listed among the documents, before the game's settings, where a
    // start lists it, so it opens read only (D843).
    if (std::ranges::find(scenes_, path) == scenes_.end()) {
        const auto kSettings = std::ranges::find(listed_, Listed::Settings);
        const auto kAt = kSettings - listed_.begin();
        scenes_.insert(scenes_.begin() + kAt, path);
        sceneSources_.insert(sceneSources_.begin() + kAt, std::string{});
        listed_.insert(kSettings, Listed::Capture);
        static_cast<void>(showScenes());
    }
    emitter_.log(diagnostics::Severity::Info,
                 kCaptureKept,
                 "a capture kept under the game's directory",
                 {diagnostics::field("capture", path),
                  diagnostics::field("frames", held_.frameCaptures),
                  diagnostics::field("profiles", held_.profilerCaptures)});
    return true;
}

void ShellParticipant::readFrame(double seconds, std::string_view capture) {
    const bool kAsked = !capture.empty();
    if (!kAsked && (!paneShown(Dock::Frame) || seconds < held_.frameNextRead)) {
        return;
    }
    held_.frameNextRead = seconds + 1;
    clear(held_.frameRows);
    const auto kLine = [this](const std::string& words, std::uint32_t color) {
        if (auto made = row(held_.frameColumn, words, color, kPanel); made.has_value()) {
            held_.frameRows.push_back(*made);
        }
    };
    if (!playing_.has_value() || previewedName().empty()) {
        kLine("play the game to see what its preview's frames hold", kQuiet);
        return;
    }
    const std::string kReply = ask(drawnFrameRecord(next(), previewedName(), capture));
    const auto kParsed = document::parse(kReply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kFigures = kAnswer != nullptr ? kAnswer->find("figures") : nullptr;
    if (kFigures == nullptr || kFigures->kind() != Value::Kind::Object) {
        kLine("the preview: " + answeredOf(kReply).message, kQuiet);
        return;
    }
    // A game that draws no scene (a 2D game's canvas, D901) has no
    // figures, its passes timed all the same.
    if (kFigures->items().empty()) {
        kLine("no scene drawn: the canvas's and the UI's passes only", kQuiet);
    } else {
        const auto kOf = [kFigures](std::string_view name) {
            return std::to_string(figureOf(*kFigures, name));
        };
        kLine("draws " + kOf("draws") + ": opaque " + kOf("opaque") + " in " + kOf("opaque_batches") +
                  " batches, translucent " + kOf("translucent") + "; culled " + kOf("culled") + ", hidden " +
                  kOf("hidden") + ", unknown meshes " + kOf("unknown_meshes") + ", unknown materials " +
                  kOf("unknown_materials"),
              kText);
        kLine("lights " + kOf("lights") + " (culled " + kOf("lights_culled") + ", over the limit " +
                  kOf("lights_over_limit") + "); shadow cascades " + kOf("shadow_cascades") + " with " +
                  kOf("cascade_casters") + " casters, light shadow maps " + kOf("light_shadow_slots") + " with " +
                  kOf("light_shadow_casters"),
              kText);
        kLine("probes " + kOf("probes") + ", decals " + kOf("decals") + ", emitters " + kOf("emitters") + ", ribbons " +
                  kOf("ribbons") + ", post processes " + kOf("post_processes"),
              kText);
        std::string effects;
        for (const std::string_view kEffect : {"temporal",
                                               "fxaa",
                                               "occlusion",
                                               "bloom",
                                               "reflections",
                                               "motion_blur",
                                               "depth_of_field",
                                               "contact_shadows"}) {
            if (figureOf(*kFigures, kEffect) != 0) {
                effects += (effects.empty() ? "" : ", ") + std::string{kEffect};
            }
        }
        kLine("samples " + kOf("samples") + "; effects " + (effects.empty() ? std::string{"none"} : effects), kQuiet);
    }
    // The GPU's time for each pass it timed, where the device times them
    // (D894).
    std::size_t timed = 0;
    if (const Value* kPasses = kAnswer->find("passes"); kPasses != nullptr && kPasses->kind() == Value::Kind::Array) {
        std::string line;
        double total = 0;
        for (const Value& kPass : kPasses->items()) {
            const Value* kName = kPass.find("pass");
            const Value* kMicroseconds = kPass.find("gpuMicroseconds");
            if (kName == nullptr || kName->kind() != Value::Kind::String || kMicroseconds == nullptr) {
                continue;
            }
            const double kSpent = kMicroseconds->real().value_or(0);
            char said[64];
            std::snprintf(said, sizeof said, " %.2f ms", kSpent / 1000);
            line += (line.empty() ? "" : ", ") + *kName->text() + said;
            total += kSpent;
            ++timed;
        }
        if (timed > 0) {
            char sum[64];
            std::snprintf(sum, sizeof sum, "GPU %.2f ms: ", total / 1000);
            kLine(sum + line, kText);
        }
    }
    // A capture's steps, the first few, as the file keeps them all.
    std::size_t steps = 0;
    if (const Value* kSteps = kAnswer->find("steps"); kAsked && kSteps != nullptr) {
        steps = kSteps->items().size();
        ++held_.frameCaptures;
        kLine("kept " + std::string{capture} + ": " + std::to_string(steps) + " steps", kText);
        for (std::size_t each = 0; each < steps && each < kStepsShown; ++each) {
            const Value& kStep = kSteps->items()[each];
            const Value* kPass = kStep.find("pass");
            const Value* kMesh = kStep.find("mesh");
            const Value* kMaterial = kStep.find("material");
            const Value* kInstances = kStep.find("instances");
            kLine(std::string{"  "} + (kPass != nullptr && kPass->text() != nullptr ? *kPass->text() : "?") +
                      ": mesh " + (kMesh != nullptr && kMesh->text() != nullptr ? *kMesh->text() : "none") +
                      ", material " + std::to_string(kMaterial != nullptr ? kMaterial->integer().value_or(0) : 0) +
                      ", " + std::to_string(kInstances != nullptr ? kInstances->integer().value_or(0) : 0) + " drawn",
                  kQuiet);
        }
        say("captured the frame in " + std::string{capture});
    }
    ++held_.frameReads;
    emitter_.log(diagnostics::Severity::Info,
                 kFrameRead,
                 kAsked ? "the preview's last frame, kept as a capture" : "the preview's last frame, as it says",
                 {diagnostics::field("draws", figureOf(*kFigures, "draws")),
                  diagnostics::field("figures", kFigures->items().size()),
                  diagnostics::field("steps", steps),
                  diagnostics::field("captures", held_.frameCaptures),
                  diagnostics::field("reads", held_.frameReads),
                  diagnostics::field("passesTimed", timed)});
}

} // namespace rawframe::studio
