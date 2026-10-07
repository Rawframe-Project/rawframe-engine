#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace rawframe::studio {

void ShellParticipant::startPlaying() {
    if (stopping_.has_value()) {
        say("the last game is still stopping");
        return;
    }
    auto started = Play::start(*play_);
    if (!started.has_value()) {
        say(std::string{started.error().description()});
        return;
    }
    playing_.emplace(std::move(*started));
    ++played_;
    nextAttach_ = 0;
    static_cast<void>(words(playNode_, "Stop", kText, 14));
    say("starting the game");
}

void ShellParticipant::stopPlaying() {
    if (previewing_ && !scene_.empty()) {
        static_cast<void>(ask(previewRecord(next(), scene_, nullptr)));
    }
    previewing_ = false;
    preview_.reset();
    // Kept until both have ended: dropping a process kills it, and a
    // stopping one ends as a Host does, its records written.
    playing_->stop();
    stopping_ = std::move(playing_);
    playing_.reset();
    static_cast<void>(words(playNode_, "Play", kText, 14));
    say("game stopped");
}

void ShellParticipant::attachPlayed(double seconds) {
    if (stopping_.has_value() && stopping_->ended()) {
        stopping_.reset();
    }
    if (previewing_ || seconds < nextAttach_) {
        return;
    }
    // A preview by configuration whose game did not answer, loaded or not
    // yet started, is tried again now and then; each try may wait for its
    // reply, so not often (D453b).
    if (!playing_.has_value()) {
        if (preview_.has_value() && !scene_.empty()) {
            nextAttach_ = seconds + 2;
            attachPreview();
        }
        return;
    }
    nextAttach_ = seconds + 0.5;
    // Advanced first: a game whose server or client ended as it started is
    // launched again there, on other ports.
    if (auto started = playing_->advance(); !started.has_value()) {
        say(std::string{started.error().description()});
        return;
    }
    if (!playing_->running()) {
        say("the game ended");
        return;
    }
    const std::optional<Preview> kPreview = playing_->preview();
    if (!kPreview.has_value()) {
        return;
    }
    preview_ = kPreview;
    if (scene_.empty()) {
        return;
    }
    attachPreview();
}

void ShellParticipant::pollPicks(double seconds) {
    if (!previewing_ || !preview_.has_value() || preview_->serverEndpoint.empty() || seconds < nextPick_) {
        return;
    }
    nextPick_ = seconds + 0.25;
    const std::string kReply = ask(pickRecord(next(), scene_));
    const std::optional<std::string> kSource = pickedIn(kReply, scene_);
    const std::optional<Moved> kMoved = movedIn(kReply, scene_);
    if (kSource.has_value()) {
        if (const auto kAt = std::ranges::find(entities_, *kSource); kAt != entities_.end()) {
            ++picked_;
            showEntity(static_cast<std::size_t>(kAt - entities_.begin()));
            say("picked " + (names_[static_cast<std::size_t>(kAt - entities_.begin())].empty()
                                 ? *kSource
                                 : names_[static_cast<std::size_t>(kAt - entities_.begin())]));
        }
    }
    if (kMoved.has_value()) {
        moveDragged(*kMoved);
    }
}

void ShellParticipant::moveDragged(const Moved& moved) {
    // A press let go where it went down is a click, not a drag.
    if (std::abs(moved.x) < 0.01 && std::abs(moved.z) < 0.01) {
        return;
    }
    std::string why;
    const std::optional<Catalog::Component> kPose = componentNamed(catalog_, "rawframe.physics3d.pose", why);
    const std::optional<Value> kRead = read(scene_, "scene.read_entity", moved.source);
    const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
    if (!kPose.has_value() || components == nullptr || components->kind() != Value::Kind::Array) {
        return;
    }
    for (const Value& each : components->items()) {
        const Value* name = each.find("name");
        if (name == nullptr || name->text() == nullptr || *name->text() != kPose->name) {
            continue;
        }
        // Where it stands now, a field at its default nought, carried by the
        // drag, to the millimetre.
        std::array<double, 2> at{};
        for (const FieldShown& kPart : fieldsShown(&*kPose, each.find("fields"))) {
            if ((kPart.name == "x" || kPart.name == "z") && kPart.text.has_value()) {
                at[kPart.name == "x" ? 0 : 1] = std::strtod(kPart.text->c_str(), nullptr);
            }
        }
        std::vector<Value> operations;
        for (const auto& [kPart, kValue] : {std::pair{"x", at[0] + moved.x}, std::pair{"z", at[1] + moved.z}}) {
            Value operation = Value::object();
            operation.add("operation", Value::string("scene.set_field"));
            operation.add("entity", Value::string(moved.source));
            operation.add("component", Value::string(kPose->id));
            operation.add("field", Value::string(kPart));
            Value value = Value::object();
            value.add("real", Value::real(std::round(kValue * 1000) / 1000));
            operation.add("value", std::move(value));
            operations.push_back(std::move(operation));
        }
        ++dragged_;
        commitAll(std::move(operations), "moved by dragging", moved.source);
        return;
    }
}

void ShellParticipant::attachPreview() {
    const Answered kAttached = answeredOf(ask(previewRecord(next(), scene_, &*preview_)));
    previewing_ = kAttached.previewing;
    if (kAttached.view.has_value()) {
        view_ = kAttached.view;
    }
    showViewText();
    say(kAttached.done ? (previewing_ ? "previewing " + scene_ : "no preview") : kAttached.message);
    if (previewing_) {
        emitter_.log(diagnostics::Severity::Info,
                     kPreviewing,
                     "a scene's preview is live in a running game",
                     {diagnostics::field("scene", std::string_view{scene_})});
    }
}

} // namespace rawframe::studio
