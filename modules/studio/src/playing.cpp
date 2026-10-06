#include "shell.h"

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
