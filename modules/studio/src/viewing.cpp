#include "shell.h"

// The scene's view as Studio moves it: typed into its fields, framed on
// what is chosen, seen in 2D or out of it, and kept in or taken from a
// bookmark, each through the session's view records (D471, D792, D793,
// D796).

namespace rawframe::studio {

void ShellParticipant::setView(const std::string& part, const std::string& text) {
    // The snap is Studio's alone: no record (D471).
    if (part == "snap") {
        if (const std::optional<Snap> kSnap = snapOf(text); kSnap.has_value()) {
            snap_ = *kSnap;
            say(snap_.move == 0 && snap_.turn == 0 ? "no snap" : "drags snap to " + snapText(snap_));
        } else {
            ++refused_;
            say("snap: meters and degrees, or 0");
        }
        showViewText();
        return;
    }
    const std::optional<Value> kView = viewWith(view_, part, text);
    if (!kView.has_value()) {
        ++refused_;
        say(part + ": " + (part == "fieldOfView" ? "one number" : "three numbers"));
        showViewText();
        return;
    }
    const Answered kAnswered = answeredOf(ask(viewRecord(next(), scene_, *kView)));
    if (kAnswered.done) {
        ++viewsSet_;
        view_ = kAnswered.view;
        previewing_ = kAnswered.previewing;
        say(previewing_ ? "view set and previewed" : "view set");
    } else {
        ++refused_;
        say(kAnswered.message);
    }
    showViewText();
}

void ShellParticipant::frameChosen() {
    if (scene_.empty() || entity_.empty()) {
        say("choose an entity to frame");
        return;
    }
    const std::string kReply = ask(frameRecord(next(), scene_));
    const Answered kAnswered = answeredOf(kReply);
    if (kAnswered.done) {
        ++viewsSet_;
        view_ = kAnswered.view;
        previewing_ = kAnswered.previewing;
        const std::size_t kCount = selected_.size();
        say(kCount > 1 ? "framed " + std::to_string(kCount) + " entities" : "framed");
    } else {
        ++refused_;
        say(kAnswered.message);
    }
    showViewText();
}

void ShellParticipant::seeIn2D(bool flat) {
    if (scene_.empty()) {
        return;
    }
    const std::optional<Value> kView = flatView(view_, flat);
    if (!kView.has_value()) {
        ++refused_;
        say("the view does not read");
        return;
    }
    const Answered kAnswered = answeredOf(ask(viewRecord(next(), scene_, *kView)));
    if (kAnswered.done) {
        ++viewsSet_;
        view_ = kAnswered.view;
        previewing_ = kAnswered.previewing;
        say(flat ? "2D view" : "3D view");
    } else {
        ++refused_;
        say(kAnswered.message);
    }
    showViewText();
}

void ShellParticipant::bookmark(std::uint8_t slot, bool go) {
    if (scene_.empty()) {
        return;
    }
    const std::string kSlot = std::to_string(slot);
    if (!go) {
        // The view as it is, or as a scene not yet looked at starts.
        const std::optional<Value> kView =
            view_.has_value() && !view_->isNull() ? view_ : viewWith(std::nullopt, "fieldOfView", "60");
        const Answered kAnswered = answeredOf(ask(bookmarkRecord(next(), scene_, slot, *kView)));
        if (kAnswered.done) {
            ++shown_.bookmarksKept;
        } else {
            ++refused_;
        }
        say(kAnswered.done ? "bookmark " + kSlot + " set" : kAnswered.message);
        return;
    }
    const std::optional<Value> kView = bookmarkIn(ask(bookmarksRecord(next(), scene_)), slot);
    if (!kView.has_value()) {
        say("no bookmark " + kSlot);
        return;
    }
    const Answered kAnswered = answeredOf(ask(viewRecord(next(), scene_, *kView)));
    if (kAnswered.done) {
        ++viewsSet_;
        ++shown_.bookmarksVisited;
        view_ = kAnswered.view;
        previewing_ = kAnswered.previewing;
        say("bookmark " + kSlot);
    } else {
        ++refused_;
        say(kAnswered.message);
    }
    showViewText();
}

} // namespace rawframe::studio
