#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
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

std::optional<std::map<std::string, double, std::less<>>> ShellParticipant::poseOf(const std::string& source) {
    std::string why;
    const std::optional<Catalog::Component> kPose = componentNamed(catalog_, "rawframe.physics3d.pose", why);
    const std::optional<Value> kRead = read(scene_, "scene.read_entity", source);
    const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
    if (!kPose.has_value() || components == nullptr || components->kind() != Value::Kind::Array) {
        return std::nullopt;
    }
    for (const Value& each : components->items()) {
        const Value* name = each.find("name");
        if (name == nullptr || name->text() == nullptr || *name->text() != kPose->name) {
            continue;
        }
        // A field at its default nought (a turn of all noughts being none).
        std::map<std::string, double, std::less<>> pose{
            {"x", 0}, {"y", 0}, {"z", 0}, {"qx", 0}, {"qy", 0}, {"qz", 0}, {"qw", 0}};
        for (const FieldShown& kPart : fieldsShown(&*kPose, each.find("fields"))) {
            if (pose.contains(kPart.name) && kPart.text.has_value()) {
                pose[kPart.name] = std::strtod(kPart.text->c_str(), nullptr);
            }
        }
        return pose;
    }
    return std::nullopt;
}

void ShellParticipant::markChosen() {
    if (!previewing_ || scene_.empty()) {
        return;
    }
    std::optional<std::array<double, 3>> at;
    if (!entity_.empty()) {
        if (const auto kPose = poseOf(entity_); kPose.has_value()) {
            at = std::array<double, 3>{kPose->at("x"), kPose->at("y"), kPose->at("z")};
        }
    }
    const std::string kReply = ask(markRecord(next(), scene_, at));
    marked_ += kReply.find("\"marked\":true") != std::string::npos ? 1 : 0;
}

void ShellParticipant::moveDragged(const Moved& moved) {
    // A press let go where it went down is a click, not a drag.
    if (std::abs(moved.x) < 0.01 && std::abs(moved.y) < 0.01 && std::abs(moved.z) < 0.01) {
        return;
    }
    std::string why;
    const std::optional<Catalog::Component> kPose = componentNamed(catalog_, "rawframe.physics3d.pose", why);
    std::optional<std::map<std::string, double, std::less<>>> held = poseOf(moved.source);
    if (!kPose.has_value() || !held.has_value()) {
        return;
    }
    std::map<std::string, double, std::less<>>& pose = *held;
    std::vector<std::pair<std::string, double>> changed;
    std::string done;
    if (moved.how == Moved::How::Move) {
        changed = {{"x", pose["x"] + moved.x}, {"z", pose["z"] + moved.z}};
        done = "moved by dragging";
    } else if (moved.how == Moved::How::Height) {
        changed = {{"y", pose["y"] + moved.y}};
        done = "raised by dragging";
        ++raised_;
    } else {
        // The angle about +Y from where it was pressed to where it was
        // let go, seen from the entity's place; composed before its turn.
        const double kTurn = std::atan2(moved.from[2] - pose["z"], moved.from[0] - pose["x"]) -
                             std::atan2(moved.to[2] - pose["z"], moved.to[0] - pose["x"]);
        const bool kNone = pose["qx"] == 0 && pose["qy"] == 0 && pose["qz"] == 0 && pose["qw"] == 0;
        const std::array<double, 4> kWas = kNone
                                               ? std::array<double, 4>{0, 0, 0, 1}
                                               : std::array<double, 4>{pose["qx"], pose["qy"], pose["qz"], pose["qw"]};
        const double kSin = std::sin(kTurn / 2);
        const double kCos = std::cos(kTurn / 2);
        // (0, s, 0, c) times (x, y, z, w).
        changed = {{"qx", kCos * kWas[0] + kSin * kWas[2]},
                   {"qy", kCos * kWas[1] + kSin * kWas[3]},
                   {"qz", kCos * kWas[2] - kSin * kWas[0]},
                   {"qw", kCos * kWas[3] - kSin * kWas[1]}};
        done = "turned by dragging";
        ++turned_;
    }
    std::vector<Value> operations;
    for (const auto& [kPart, kValue] : changed) {
        Value operation = Value::object();
        operation.add("operation", Value::string("scene.set_field"));
        operation.add("entity", Value::string(moved.source));
        operation.add("component", Value::string(kPose->id));
        operation.add("field", Value::string(kPart));
        Value value = Value::object();
        // Places to the millimetre, turns to a millionth.
        const double kStep = kPart.starts_with('q') ? 1e6 : 1e3;
        value.add("real", Value::real(std::round(kValue * kStep) / kStep));
        operation.add("value", std::move(value));
        operations.push_back(std::move(operation));
    }
    ++dragged_;
    commitAll(std::move(operations), done, moved.source);
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
        markChosen();
    }
}

} // namespace rawframe::studio
