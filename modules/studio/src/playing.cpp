#include "rawframe/authoring_session/cooked_copy.h"
#include "rawframe/render/device.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <numbers>
#include <string>
#include <utility>

namespace rawframe::studio {

namespace {

/// The picture a game streams into a viewport of `at`: its size, made
/// smaller where its pixels would pass the UI's share of a frame's uploads
/// and so never show (D927); drawn stretched over the viewport.
/// A rectangle of the layout in the window's device pixels (D963): the
/// viewport's picture is drawn at those, as sharp as the screen.
ui::Rect devicePixels(ui::Rect at, float scale) {
    return ui::Rect{.x = at.x * scale, .y = at.y * scale, .width = at.width * scale, .height = at.height * scale};
}

std::array<std::uint32_t, 2> streamedSize(const ui::Rect& at) {
    const double kWidth = at.width >= 64 ? std::floor(at.width) : 640.0;
    const double kHeight = at.height >= 64 ? std::floor(at.height) : 360.0;
    // Each row padded as a device copies it, 256 bytes.
    const auto kBytes = [](double width, double height) {
        return std::ceil(width * 4 / 256) * 256 * height;
    };
    const double kMost = render::kUiImageUploadBytes;
    double scale = kBytes(kWidth, kHeight) > kMost ? std::sqrt(kMost / kBytes(kWidth, kHeight)) : 1.0;
    while (scale > 0.1 && kBytes(std::floor(kWidth * scale), std::floor(kHeight * scale)) > kMost) {
        scale -= 0.01;
    }
    return {static_cast<std::uint32_t>(std::max(64.0, std::floor(kWidth * scale))),
            static_cast<std::uint32_t>(std::max(64.0, std::floor(kHeight * scale)))};
}

/// Whether a game's description makes it a 2D one: its physics the 2D
/// engine's, none of the 3D's (D927).
bool flatGame(const std::filesystem::path& description) {
    const std::string kText = authoring_session::readFile(description).value_or("");
    const auto kSays = [&kText](std::string_view word) {
        for (std::size_t at = kText.find(word); at != std::string::npos; at = kText.find(word, at + 1)) {
            if ((at == 0 || kText[at - 1] == '\n') && at + word.size() < kText.size() &&
                (kText[at + word.size()] == ' ' || kText[at + word.size()] == '\n')) {
                return true;
            }
        }
        return false;
    };
    return kSays("physics2d") && !kSays("physics3d");
}

} // namespace

void ShellParticipant::settleFlat() {
    // A 2D game's scene opens on the 2D view, as an editor opens a 2D
    // project (D927), each scene it opens framed whole (D933).
    if (!std::exchange(held_.flatPending, false) || !chosen(Listed::Scene) || !flatGame(description_)) {
        return;
    }
    seeIn2D(true);
    // Quietly: a scene of nothing or only instances has nothing of its own
    // to frame.
    if (held_.flat && entity_.empty()) {
        frameChosen(true);
    }
}

void ShellParticipant::keepEditing() {
    if (!play_.has_value() || playing_.has_value() || stopping_.has_value()) {
        return;
    }
    // Play asked for cooks first where the content is behind, as an
    // editor builds before it runs (D923); joined, it plays what this
    // author cooked at the host (D905, D925).
    if (playAsked_) {
        if (cooking_.has_value()) {
            return;
        }
        if (!std::exchange(playCooked_, true) && cookBehind()) {
            cookOrStop();
            return;
        }
        playAsked_ = false;
        playCooked_ = false;
        startPlaying();
        return;
    }
    if (!editView_ || broken_.has_value() || !gameOpen() || held_.joined != nullptr) {
        return;
    }
    // A game opened opens on its first scene, as an editor opens a
    // project on its main one (D912).
    if (scene_.empty() && !std::exchange(editOpened_, true)) {
        if (const auto kFirst = std::ranges::find(listed_, Listed::Scene);
            kFirst != listed_.end() && static_cast<std::size_t>(kFirst - listed_.begin()) < scenes_.size()) {
            showScene(static_cast<std::size_t>(kFirst - listed_.begin()));
        } else if (!documentChosen()) {
            // A game with no scene, its world made in Kest, still has its
            // viewport, where Play shows it (D923).
            clear(viewRows_);
            showViewport();
        }
    }
    if (cooking_.has_value()) {
        return;
    }
    // Cooked once on opening where it is behind, as an editor imports what
    // it opens: the scene's view, the Assets' thumbnails and a material's
    // preview are drawn from the cook (D927).
    if (!editCooked_ && cookBehind()) {
        editCooked_ = true;
        cookOrStop();
        return;
    }
    if (scene_.empty() || !chosen(Listed::Scene)) {
        return;
    }
    // Drawn from the game's content as Studio cooks it: none before a cook.
    std::error_code error;
    if (!cookTool_.empty() && !std::filesystem::is_regular_file(cookAt_ / "content" / "content.manifest", error)) {
        return;
    }
    startPlaying(true);
}

bool ShellParticipant::cookBehind() const {
    if (cookTool_.empty() || held_.joined != nullptr) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(cookAt_ / "content" / "content.manifest", error)) {
        return true;
    }
    return std::ranges::any_of(browser_.sources, [](const ListedSource& source) {
        return source.cook != "cooked" && source.cook != "failed";
    });
}

void ShellParticipant::startPlaying(bool edited) {
    if (stopping_.has_value()) {
        say("the last game is still stopping");
        return;
    }
    // At a host joined, what this author cooked there is played here
    // (D905).
    if (held_.joined != nullptr && !bringCooked()) {
        return;
    }
    // In the viewport at its size, where one is shown (D789), unless it is
    // tried as a device, in a window of the device's size (D809).
    play_->viewport.reset();
    play_->device = device_;
    if (inViewport_.inViewport && !device_.has_value()) {
        play_->viewport = streamedSize(
            devicePixels(tree_->contains(viewport_) ? tree_->placedRectOf(viewport_) : ui::Rect{}, scale_));
    }
    // With the players chosen, each a client (D804); edited, one, in the
    // viewport (D912).
    play_->players = edited ? 1 : players_;
    play_->edited = edited;
    if (edited && !play_->viewport.has_value()) {
        play_->viewport = streamedSize(
            devicePixels(tree_->contains(viewport_) ? tree_->placedRectOf(viewport_) : ui::Rect{}, scale_));
    }
    auto started = Play::start(*play_);
    if (!started.has_value()) {
        say(std::string{started.error().description()});
        return;
    }
    playing_.emplace(std::move(*started));
    ++played_;
    // Its logs read into the Console as they grow, each from its start
    // (D778).
    tails_.clear();
    tails_.emplace_back(playing_->directory() / "server.log", ConsoleLine::Source::Server);
    tails_.emplace_back(playing_->directory() / "client.log", ConsoleLine::Source::Client);
    nextAttach_ = 0;
    if (edited) {
        say("showing " + scene_);
        return;
    }
    relabel(playNode_, "Stop", Icon::Stop);
    showViewBar();
    say("starting the game");
}

bool ShellParticipant::bringCooked() {
    if (const result::Status kMade = privateDirectory(cookAt_); !kMade.has_value()) {
        say(std::string{kMade.error().description()});
        return false;
    }
    // By the plain name Studio cooks under at a host (D885), each piece
    // asked as any record is.
    const auto kCopied = authoring_session::copyCooked(
        [this](Value record) {
            record.add("id", Value::integer(next()));
            return ask(record);
        },
        "content",
        cookAt_ / "content");
    if (!kCopied.has_value()) {
        say("the cook cannot be brought here: " + std::string{kCopied.error().description()});
        return false;
    }
    emitter_.log(diagnostics::Severity::Info,
                 kCookBrought,
                 "the cook made at the host joined brought here to be played",
                 {diagnostics::field("copied", kCopied->copied),
                  diagnostics::field("kept", kCopied->kept),
                  diagnostics::field("bytes", kCopied->bytes)});
    return true;
}

void ShellParticipant::runPlayed(bool step) {
    if (!playing_.has_value() || previewedName().empty()) {
        say("play the game to pause it");
        return;
    }
    // A step pauses first, as a debugger's does.
    if (step && !inViewport_.paused) {
        runPlayed(false);
    }
    const std::string kReply =
        ask(step ? stepRecord(next(), previewedName(), 1) : runRecord(next(), previewedName(), !inViewport_.paused));
    const auto kParsed = document::parse(kReply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kPaused = kAnswer != nullptr ? kAnswer->find("paused") : nullptr;
    const Value* kTick = kAnswer != nullptr ? kAnswer->find("tick") : nullptr;
    if (kPaused == nullptr || kTick == nullptr) {
        say(answeredOf(kReply).message);
        return;
    }
    const bool kWasPaused = inViewport_.paused;
    inViewport_.paused = kPaused->truth().value_or(false);
    inViewport_.tick = kTick->integer().value_or(0);
    if (!kWasPaused && inViewport_.paused) {
        inViewport_.pausedAt = inViewport_.tick;
    } else if (kWasPaused && !inViewport_.paused) {
        inViewport_.ranPaused = inViewport_.tick - inViewport_.pausedAt;
    }
    ++(step ? inViewport_.steps : inViewport_.pauses);
    // The tick the step runs is the next one; it has run by the next frame.
    say(step ? "stepped from tick " + std::to_string(inViewport_.tick)
             : (inViewport_.paused ? "paused at tick " : "running from tick ") + std::to_string(inViewport_.tick));
}

void ShellParticipant::stopPlaying() {
    if (previewing_ && !previewedName().empty()) {
        static_cast<void>(ask(previewRecord(next(), previewedName(), nullptr)));
    }
    previewing_ = false;
    preview_.reset();
    // Kept until both have ended: dropping a process kills it, and a
    // stopping one ends as a Host does, its records written.
    playing_->stop();
    stopping_ = std::move(playing_);
    playing_.reset();
    // The scene's own entities again in the Hierarchy (D923).
    if (live_.on) {
        switchLive();
    }
    relabel(playNode_, "Play", Icon::Play);
    showViewBar();
    say("game stopped");
}

void ShellParticipant::cookOrStop() {
    if (cooking_.has_value()) {
        static_cast<void>(ask(cancelRecord(next(), *cooking_)));
        say("stopping the cook");
        return;
    }
    const std::int64_t kId = next();
    std::string refused;
    if (held_.joined != nullptr) {
        // At a host joined, into the host's cooking directory, by plain
        // names it keeps in this author's own (D885).
        refused = ask(cookRecord(kId, "content", "cook-cache"));
    } else {
        // Into the game's play directory, its owner's alone (D462).
        if (const result::Status kMade = privateDirectory(cookAt_); !kMade.has_value()) {
            say(std::string{kMade.error().description()});
            return;
        }
        refused = ask(cookRecord(kId, cookAt_ / "content", cookAt_ / "cook-cache"));
    }
    const std::string& kRefused = refused;
    // A cook started answers when it ends; one refused, at once.
    if (!kRefused.empty()) {
        say(answeredOf(kRefused).message);
        return;
    }
    cooking_ = kId;
    relabel(cookNode_, "Stop");
    say("cooking");
}

void ShellParticipant::hearCook() {
    if (session_ == nullptr) {
        return;
    }
    for (const std::string& kLine : session_->poll()) {
        hearCookLine(kLine);
    }
}

void ShellParticipant::hearCookLine(const std::string& line) {
    const Heard kHeard = heardOf(line);
    if (!cooking_.has_value() || kHeard.id != *cooking_ || kHeard.kind == Heard::Kind::None) {
        return;
    }
    switch (kHeard.kind) {
    case Heard::Kind::None:
        break;
    case Heard::Kind::Progress:
        say("cooking " + std::to_string(kHeard.step) + "/" + std::to_string(kHeard.steps) + " " + kHeard.text);
        return;
    case Heard::Kind::Cooked:
        say("cooked " + std::to_string(kHeard.cooked + kHeard.reused) + " sources, " + std::to_string(kHeard.reused) +
            " reused");
        break;
    case Heard::Kind::Failed:
        say(kHeard.text);
        playAsked_ = false;
        break;
    case Heard::Kind::Cancelled:
        say("cook stopped");
        playAsked_ = false;
        break;
    }
    cooking_.reset();
    relabel(cookNode_, "Cook");
    // Each source as the cook left it (D799), its thumbnail from this cook
    // (D801).
    listSources();
    browser_.pictures.clear();
    static_cast<void>(showScenes());
    if (std::exchange(recook_, false)) {
        cookOrStop();
    }
}

void ShellParticipant::recookPlayed() {
    std::error_code error;
    if ((!chosen(Listed::Material) && !chosen(Listed::Graph)) || !playing_.has_value() || cookTool_.empty() ||
        !std::filesystem::is_regular_file(cookAt_ / "content" / "content.manifest", error)) {
        return;
    }
    if (cooking_.has_value()) {
        recook_ = true;
        return;
    }
    cookOrStop();
}

void ShellParticipant::attachPlayed(double seconds) {
    // Said, so what closes Studio after a game waits for the game's own
    // stop, its records written, rather than ending it mid-stop (D504).
    if (stopping_.has_value() && stopping_->ended()) {
        stopping_.reset();
        emitter_.log(diagnostics::Severity::Info, kGameEnded, "the stopped game has ended");
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
    // At a host joined, the preview is held beside the link to it, the
    // game playing here (D906).
    const std::optional<Preview> kPreview = playing_->preview();
    if (!kPreview.has_value()) {
        return;
    }
    preview_ = kPreview;
    // A game played, not edited, shows its own camera, as a game window
    // does (D916): observed, so the Hierarchy lists its World live
    // (D923); only the edited one is the editor's view.
    preview_->observe = editView_ && !playing_->edited();
    attachPreview();
}

void ShellParticipant::pollPicks(double seconds) {
    if (!previewing_ || !preview_.has_value() || preview_->serverEndpoint.empty() || preview_->observe ||
        seconds < nextPick_) {
        return;
    }
    // Often, as nothing waits for the preview's answer (D969).
    nextPick_ = seconds + 0.05;
    const std::string kReply = ask(pickRecord(next(), scene_));
    // Said once each time another part is lit, so what waits on a hover
    // waits for it (D504).
    if (std::string lit = litIn(kReply); lit != lit_) {
        lit_ = std::move(lit);
        if (!lit_.empty()) {
            emitter_.log(diagnostics::Severity::Info,
                         kLit,
                         "a part of the mark is lit under the pointer",
                         {diagnostics::field("part", std::string_view{lit_})});
        }
    }
    const std::optional<std::string> kSource = pickedIn(kReply, scene_);
    const std::optional<Moved> kMoved = movedIn(kReply, scene_);
    // With an extension's mode on, a click that met the scene runs it
    // there, on what it met, instead of choosing that (D877).
    const std::optional<std::array<double, 3>> kPoint = pickedPointIn(kReply);
    if (held_.extensionMode.has_value() && kPoint.has_value()) {
        runExtensionMode(*kPoint, kSource);
    } else if (kSource.has_value()) {
        if (const std::optional<std::string> kLayer = lockedOf(*kSource); kLayer.has_value()) {
            // Not picked in the viewport, nor moved there (D788).
            ++lockRefused_;
            say(*kLayer + " is locked");
        } else if (const auto kAt = std::ranges::find(entities_, *kSource); kAt != entities_.end()) {
            ++picked_;
            showEntity(static_cast<std::size_t>(kAt - entities_.begin()));
            say("picked " + (names_[static_cast<std::size_t>(kAt - entities_.begin())].empty()
                                 ? *kSource
                                 : names_[static_cast<std::size_t>(kAt - entities_.begin())]));
        }
    }
    if (kMoved.has_value() && !lockedOf(kMoved->source).has_value()) {
        moveDragged(*kMoved);
    }
    // The view moved in the played game, shown in the view's fields
    // (D469).
    if (auto viewed = viewedIn(kReply); viewed.has_value()) {
        view_ = std::move(viewed);
        ++viewsMoved_;
        showViewText();
        const Value* kEye = view_->find("eye");
        const Value* kTarget = view_->find("target");
        double distance = 0;
        if (kEye != nullptr && kTarget != nullptr && kEye->items().size() == 3 && kTarget->items().size() == 3) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double kApart =
                    kEye->items()[axis].real().value_or(0) - kTarget->items()[axis].real().value_or(0);
                distance += kApart * kApart;
            }
        }
        // Where the eye and the target are now too (D472).
        const auto kAt = [](const Value* point, std::size_t axis) {
            return point != nullptr && point->items().size() == 3 ? point->items()[axis].real().value_or(0) : 0.0;
        };
        emitter_.log(diagnostics::Severity::Info,
                     kViewMoved,
                     "the view moved in the played game",
                     {diagnostics::field("distance", std::sqrt(distance)),
                      diagnostics::field("eyeX", kAt(kEye, 0)),
                      diagnostics::field("eyeY", kAt(kEye, 1)),
                      diagnostics::field("eyeZ", kAt(kEye, 2)),
                      diagnostics::field("targetX", kAt(kTarget, 0)),
                      diagnostics::field("targetY", kAt(kTarget, 1)),
                      diagnostics::field("targetZ", kAt(kTarget, 2))});
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
    if (kReply.find("\"marked\":true") == std::string::npos) {
        return;
    }
    ++marked_;
    if (at.has_value()) {
        emitter_.log(diagnostics::Severity::Info,
                     kMarked,
                     "the preview marks where the chosen entity stands",
                     {diagnostics::field("entity", std::string_view{entity_})});
    }
}

void ShellParticipant::outlineChosen() {
    if (!previewing_ || scene_.empty()) {
        outlined_.clear();
        return;
    }
    const auto kOutline = [](std::uint32_t color, const std::vector<std::string>& chosen) {
        Value rgb = Value::array();
        for (const unsigned kShift : {24U, 16U, 8U}) {
            rgb.push(Value::real(static_cast<double>((color >> kShift) & 0xFFU) / 255));
        }
        Value entities = Value::array();
        for (const std::string& kEntity : chosen) {
            entities.push(Value::string(kEntity));
        }
        Value made = Value::object();
        made.add("color", std::move(rgb));
        made.add("entities", std::move(entities));
        return made;
    };
    // This author's choice in the color they are shown in at a host, else
    // the Hierarchy's; then each other's at this scene, in theirs.
    Value outlines = Value::array();
    outlines.push(kOutline(held_.joinedAs.empty() ? kChosen : authorColorOf(held_.joinedAs), selected_));
    for (const Collaborating& kOther : held_.collaborators) {
        if (kOther.author != held_.joinedAs && kOther.document == scene_ && !kOther.chosen.empty() &&
            outlines.items().size() < 16) {
            outlines.push(kOutline(authorColorOf(kOther.author), kOther.chosen));
        }
    }
    std::string written = document::writeCompact(outlines);
    if (written == outlined_) {
        return;
    }
    outlined_ = std::move(written);
    Value record = Value::object();
    record.add("kind", Value::string("authoring.outline"));
    record.add("id", Value::integer(next()));
    record.add("scene", Value::string(scene_));
    record.add("outlines", outlines);
    if (ask(record).find("\"shown\":true") == std::string::npos) {
        return;
    }
    ++outlinesShown_;
    emitter_.log(diagnostics::Severity::Info,
                 kOutlined,
                 "the preview outlines what each author has chosen",
                 {diagnostics::field("authors", outlines.items().size()), diagnostics::field("shown", outlinesShown_)});
}

void ShellParticipant::scaleDragged(const Moved& moved, std::map<std::string, double, std::less<>>& pose) {
    // Sized by how far the drag went to the view's right (or up, by the
    // height handle): a meter doubles it, a meter back halves it (D922).
    std::array<double, 2> right{1, 0};
    if (view_.has_value()) {
        const Value* kEye = view_->find("eye");
        const Value* kTarget = view_->find("target");
        if (kEye != nullptr && kTarget != nullptr && kEye->items().size() == 3 && kTarget->items().size() == 3) {
            const double kX = kTarget->items()[0].real().value_or(0) - kEye->items()[0].real().value_or(0);
            const double kZ = kTarget->items()[2].real().value_or(0) - kEye->items()[2].real().value_or(0);
            const double kLength = std::hypot(kX, kZ);
            if (kLength > 1e-6) {
                right = {-kZ / kLength, kX / kLength};
            }
        }
    }
    const double kAlong = moved.how == Moved::How::Height ? moved.y : moved.x * right[0] + moved.z * right[1];
    const double kFactor = std::clamp(std::exp2(kAlong), 0.05, 20.0);
    static_cast<void>(pose);
    std::string why;
    const std::optional<Catalog::Component> kBody = componentNamed(catalog_, "rawframe.physics3d.body", why);
    const std::optional<Value> kRead = read(scene_, "scene.read_entity", moved.source);
    const Value* kComponents = kRead.has_value() ? kRead->find("components") : nullptr;
    if (!kBody.has_value() || kComponents == nullptr) {
        say("only a body is sized by dragging");
        return;
    }
    std::vector<Value> operations;
    for (const Value& kEach : kComponents->items()) {
        const Value* kId = kEach.find("component");
        if (kId == nullptr || kId->text() == nullptr || *kId->text() != kBody->id) {
            continue;
        }
        const std::vector<FieldShown> kSizes = fieldsShown(&*kBody, kEach.find("fields"));
        for (const std::string_view kSide : {"width", "height", "depth"}) {
            const auto kSize = std::ranges::find(kSizes, kSide, &FieldShown::name);
            double now = 0.5;
            if (kSize != kSizes.end() && kSize->text.has_value()) {
                now = std::strtod(kSize->text->c_str(), nullptr);
            }
            Value operation = Value::object();
            operation.add("operation", Value::string("scene.set_field"));
            operation.add("entity", Value::string(moved.source));
            operation.add("component", Value::string(kBody->id));
            operation.add("field", Value::string(std::string{kSide}));
            Value value = Value::object();
            value.add("real", Value::real(std::max(0.01, std::round(now * kFactor * 1000) / 1000)));
            operation.add("value", std::move(value));
            operations.push_back(std::move(operation));
        }
    }
    if (operations.empty()) {
        say("only a body is sized by dragging");
        return;
    }
    ++dragged_;
    commitAll(std::move(operations), "sized by dragging", moved.source);
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
    // A place moved is snapped to the grid where it moved, a turn to
    // whole steps (D471).
    const auto kPlaced = [this](double was, double by) {
        return by != 0 ? snapped(was + by, snap_.move) : was;
    };
    // The toolbar's tool says what a drag does (D922): Select only
    // chooses, Rotate turns whatever was grabbed, Scale sizes the body.
    if (held_.tool == 0) {
        return;
    }
    if (held_.tool == 3) {
        scaleDragged(moved, pose);
        return;
    }
    const Moved::How kHow = held_.tool == 2 ? Moved::How::Turn : moved.how;
    if (kHow == Moved::How::Move) {
        changed = {{"x", kPlaced(pose["x"], moved.x)}, {"z", kPlaced(pose["z"], moved.z)}};
        done = "moved by dragging";
    } else if (kHow == Moved::How::Height) {
        changed = {{"y", kPlaced(pose["y"], moved.y)}};
        done = "raised by dragging";
        ++raised_;
    } else {
        // The angle about +Y from where it was pressed to where it was
        // let go, seen from the entity's place; composed before its turn.
        const double kTurn = snapped(std::atan2(moved.from[2] - pose["z"], moved.from[0] - pose["x"]) -
                                         std::atan2(moved.to[2] - pose["z"], moved.to[0] - pose["x"]),
                                     snap_.turn * std::numbers::pi / 180);
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
    snapped_ += snap_.move > 0 || snap_.turn > 0 ? 1 : 0;
    commitAll(std::move(operations), done, moved.source);
    emitter_.log(diagnostics::Severity::Info,
                 kDragged,
                 "a drag in the played game was committed",
                 {diagnostics::field("entity", std::string_view{moved.source}), diagnostics::field("how", done)});
}

void ShellParticipant::attachPreview() {
    if (documentChosen()) {
        return;
    }
    const Answered kAttached = answeredOf(ask(previewRecord(next(), previewedName(), &*preview_)));
    previewing_ = kAttached.previewing;
    if (kAttached.view.has_value()) {
        view_ = kAttached.view;
    }
    // Edited, it is seen through the editor's camera, on the whole scene
    // where the scene keeps no view of its own (D913).
    if (previewing_ && playing_.has_value() && playing_->edited() && !view_.has_value()) {
        Answered framed = answeredOf(ask(frameRecord(next(), scene_)));
        // Nothing in it stands anywhere: from above the ground's middle.
        if (!framed.done) {
            Value standing = Value::object();
            const auto kPoint = [](double x, double y, double z) {
                Value point = Value::array();
                point.push(Value::real(x));
                point.push(Value::real(y));
                point.push(Value::real(z));
                return point;
            };
            standing.add("eye", kPoint(12, 9, 12));
            standing.add("target", kPoint(0, 0, 0));
            standing.add("fieldOfView", Value::real(60));
            framed = answeredOf(ask(viewRecord(next(), scene_, standing)));
        }
        if (framed.done) {
            view_ = framed.view;
        }
    }
    // With the ground's grid, as an editor's viewport has it (D914).
    if (previewing_ && playing_.has_value() && playing_->edited() && !std::exchange(held_.gridShown, true) &&
        std::ranges::find(shown_.overlays, "grid") == shown_.overlays.end()) {
        shown_.overlays.insert(shown_.overlays.begin(), "grid");
    }
    showViewText();
    showViewBar();
    if (!preview_->observe) {
        say(kAttached.done ? (previewing_ ? "previewing " + previewedName() : "no preview") : kAttached.message);
    }
    // A game played lists its World live in the Hierarchy, as an
    // editor's play mode does (D923).
    if (previewing_ && preview_->observe && !live_.on) {
        switchLive();
    }
    if (previewing_) {
        emitter_.log(diagnostics::Severity::Info,
                     kPreviewing,
                     "a scene's preview is live in a running game",
                     {diagnostics::field("scene", std::string_view{scene_})});
        markChosen();
        // And without the layers hidden before it was attached (D791).
        if (!shown_.hiddenLayers.empty()) {
            tellHidden();
        }
        // And drawn as it was asked to be (D794).
        if (shown_.viewMode != "lit") {
            tellViewMode();
        }
        // And split as it was (D797).
        if (shown_.views != 1) {
            tellSplit();
        }
        // And with its overlays (D798).
        if (!shown_.overlays.empty()) {
            tellOverlays();
        }
    }
}

} // namespace rawframe::studio
