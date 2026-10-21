#include "rawframe/authoring_session/game.h"
#include "rawframe/render/capture.h"
#include "rawframe/scene/scene.h"
#include "rawframe/world_tooling/window_input.h"
#include "shell.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace rawframe::studio {

void ShellParticipant::followPicture() {
    if (!playing_.has_value() || !play_.has_value() || !play_->viewport.has_value()) {
        return;
    }
    // The newest picture the client streamed, when the file changed: it
    // is put there whole, so what reads is one picture or none.
    const std::filesystem::path kPath = playing_->picture();
    std::error_code error;
    const auto kWritten = std::filesystem::last_write_time(kPath, error);
    if (error || (inViewport_.pictureWritten.has_value() && *inViewport_.pictureWritten == kWritten)) {
        return;
    }
    inViewport_.pictureWritten = kWritten;
    // In large blocks: a character at a time, four megabytes took a tenth
    // of a second on Windows, which held a moving view under ten pictures
    // a second (D964).
    const auto kStarted = std::chrono::steady_clock::now();
    const std::optional<std::string> kBytes = authoring_session::readFile(kPath);
    if (!kBytes.has_value()) {
        return;
    }
    std::optional<render::StreamedPicture> read =
        render::streamedFrom(std::as_bytes(std::span{kBytes->data(), kBytes->size()}));
    const auto kTook = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - kStarted).count());
    inViewport_.readMicroseconds += kTook;
    inViewport_.slowestReadMicroseconds = std::max(inViewport_.slowestReadMicroseconds, kTook);
    if (!read.has_value() || read->sequence == inViewport_.pictureSequence) {
        return;
    }
    inViewport_.pictureSequence = read->sequence;
    // Shown opaque: a camera's picture, whatever its device left in its
    // alpha (Direct3D 12's software adapter leaves nought, D923).
    for (std::size_t at = 3; at < read->pixels.size(); at += 4) {
        read->pixels[at] = std::byte{0xFF};
    }
    texture::Texture made{.format = texture::Format::Rgba8Srgb};
    made.levels.push_back(
        texture::Level{.width = read->width, .height = read->height, .bytes = std::move(read->pixels)});
    const bool kFirst = inViewport_.picture == nullptr;
    inViewport_.picture = std::make_shared<const texture::Texture>(std::move(made));
    ++inViewport_.picturesShown;
    // Kept once an opening, a few pictures in, as the start screen's tile
    // for the game (D927).
    if (!held_.gamePictured && inViewport_.picturesShown >= 12) {
        held_.gamePictured = true;
        keepGamePicture(*inViewport_.picture);
    }
    // The game's first picture: it plays now, which the status line says
    // until something else is said (D923).
    if (kFirst && !playing_->edited()) {
        say("playing " + description_.stem().string());
    }
}

void ShellParticipant::showViewport() {
    showPrefabBar();
    // Under the view's fields, the rest of the center: the played game's
    // pictures while it plays here (D789).
    auto made = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0), .minHeight = ui::pixels(0), .grow = 1, .shrink = 1},
                    kBackground);
    if (!made.has_value()) {
        return;
    }
    viewport_ = *made;
    viewRows_.push_back(viewport_);
    static_cast<void>(tree_->setLook(viewport_, ui::Look{.fill = 0x0B0D10FFU, .image = kViewportImage}));
    static_cast<void>(tree_->setName(viewport_, "Viewport"));
    showViewBar();
    // What is drawn over it last, so it lies on top (D915), under the
    // prefab bar where there is one (D932).
    if (tree_->contains(held_.modePalette)) {
        static_cast<void>(
            tree_->setLayout(held_.modePalette,
                             ui::Layout{.direction = ui::Direction::Column,
                                        .gap = 2,
                                        .padding = {4, 4, 4, 4},
                                        .border = {1, 1, 1, 1},
                                        .placement = {.absolute = true,
                                                      .x = ui::pixels(8),
                                                      .y = ui::pixels(look::kTabHeight + 44 + held_.prefabTall)}}));
        static_cast<void>(tree_->detach(held_.modePalette));
        static_cast<void>(tree_->attach(centerColumn_, held_.modePalette));
    }
}

void ShellParticipant::showPrefabBar() {
    // A scene other scenes place is a prefab's source (D932): over its
    // view, the way back to the scene it was opened from, and how far an
    // edit here reaches, as the design's Prefab screen.
    held_.prefabBack = ui::Node{};
    held_.prefabTall = 0;
    if (!chosen(Listed::Scene) || sceneAt_ >= sceneSources_.size() || sceneSources_[sceneAt_].empty()) {
        return;
    }
    const base::Bits128Parse kIdentity = base::parseBits128Hex(sceneSources_[sceneAt_]);
    if (!kIdentity.parsed) {
        return;
    }
    std::size_t instances = 0;
    std::size_t scenes = 0;
    for (std::size_t each = 0; each < scenes_.size() && each < listed_.size(); ++each) {
        if (listed_[each] != Listed::Scene || each == sceneAt_) {
            continue;
        }
        const auto kSceneText = authoring_session::readFile(sceneRoot_ / scenes_[each]);
        if (!kSceneText.has_value()) {
            continue;
        }
        const auto kRead = scene::readScene(*kSceneText);
        if (!kRead.has_value()) {
            continue;
        }
        const auto kHere = static_cast<std::size_t>(
            std::ranges::count(kRead->instances, kIdentity.value, &scene::SceneInstance::scene));
        instances += kHere;
        scenes += kHere > 0 ? 1 : 0;
    }
    if (instances == 0) {
        return;
    }
    auto bar = box(centerColumn_,
                   ui::Layout{.height = ui::pixels(32),
                              .alignItems = ui::Align::Center,
                              .gap = 6,
                              .shrink = 0,
                              .padding = {12, 12, 0, 0},
                              .border = {0, 0, 0, 1}},
                   0);
    if (!bar.has_value()) {
        return;
    }
    viewRows_.push_back(*bar);
    held_.prefabTall = 32;
    static_cast<void>(tree_->setLook(*bar, ui::Look{.fill = 0x241A3AFFU, .borderColor = 0x4B3A78FFU}));
    static_cast<void>(tree_->setName(*bar, "Prefab bar"));
    if (!held_.prefabFrom.empty() && held_.prefabFrom != scene_ && std::ranges::contains(scenes_, held_.prefabFrom)) {
        if (auto back = textButton(*bar, Icon::ChevronRight, documentLabel(held_.prefabFrom), ButtonKind::Quiet);
            back.has_value()) {
            held_.prefabBack = *back;
            static_cast<void>(tree_->setName(*back, "Back to " + held_.prefabFrom));
        }
        static_cast<void>(icon(*bar, Icon::ChevronRight, look::kFaintInk, 12));
    }
    if (auto named = box(*bar, ui::Layout{.shrink = 0}, 0); named.has_value()) {
        static_cast<void>(strongWords(*named, documentLabel(scene_), look::kInk, look::kBodySize));
    }
    static_cast<void>(box(*bar, ui::Layout{.grow = 1}, 0));
    if (auto reach = box(*bar, ui::Layout{.shrink = 0}, 0); reach.has_value()) {
        static_cast<void>(words(*reach,
                                "Changes go to " + std::to_string(instances) +
                                    (instances == 1 ? " instance in " : " instances in ") + std::to_string(scenes) +
                                    (scenes == 1 ? " scene" : " scenes"),
                                0xB9A6F0FFU,
                                look::kLabelSize));
    }
}

bool ShellParticipant::pressPrefabBar(ui::Node node) {
    if (node != held_.prefabBack || node == ui::Node{}) {
        return false;
    }
    const std::string kBack = std::exchange(held_.prefabFrom, std::string{});
    if (const auto kAt = std::ranges::find(scenes_, kBack); kAt != scenes_.end()) {
        showScene(static_cast<std::size_t>(kAt - scenes_.begin()));
        say("back to " + kBack);
    }
    return true;
}

namespace {

/// A pill's menu by its name, and the commands it lists (D916).
constexpr std::string_view kProjectionPill = "Projection";
constexpr std::string_view kDrawPill = "Draw mode";
constexpr std::string_view kShowPill = "Show";
constexpr std::string_view kBookmarkPill = "Bookmarks";
constexpr std::string_view kSplitPill = "Split";

} // namespace

bool pillLists(std::string_view pill, std::string_view command) {
    if (pill == kProjectionPill) {
        return command == "3D view" || command == "2D view";
    }
    if (pill == kDrawPill) {
        return command == "Lit" || command == "Unlit" || command == "Normals";
    }
    if (pill == kShowPill) {
        return command.starts_with("Show ");
    }
    if (pill == kBookmarkPill) {
        return command.starts_with("Go to bookmark ") || command.starts_with("Set bookmark ");
    }
    if (pill == kSplitPill) {
        return command == "One view" || command == "Two views" || command == "Three views";
    }
    if (pill == "New entity") {
        return command.starts_with("New ");
    }
    return false;
}

bool isPill(std::string_view menu) {
    return menu == kProjectionPill || menu == kDrawPill || menu == kShowPill || menu == kBookmarkPill ||
           menu == kSplitPill || menu == "New entity";
}

void ShellParticipant::showViewBar() {
    for (const ui::Node kOld : {held_.viewBar, held_.splitBar, held_.axisGizmo, held_.flyHint}) {
        if (tree_->contains(kOld)) {
            static_cast<void>(tree_->remove(kOld));
        }
    }
    std::erase_if(menus_, [this](const std::pair<ui::Node, std::string>& menu) {
        return std::ranges::contains(held_.viewPills, menu);
    });
    held_.viewPills.clear();
    held_.viewBar = held_.splitBar = held_.axisGizmo = held_.flyHint = ui::Node{};
    held_.axisEnds = {};
    if (!tree_->contains(viewport_)) {
        return;
    }
    const auto kOverlay = [this](ui::Placement placement, ui::Direction direction) -> std::optional<ui::Node> {
        auto made = box(centerColumn_,
                        ui::Layout{.direction = direction,
                                   .alignItems = ui::Align::Center,
                                   .gap = 6,
                                   .padding = {8, 8, 8, 8},
                                   .placement = placement},
                        0);
        if (!made.has_value()) {
            return std::nullopt;
        }
        static_cast<void>(tree_->setInteraction(*made, ui::Interaction{.hits = ui::Interaction::Hits::Children}));
        // Gone with the viewport.
        viewRows_.push_back(*made);
        return *made;
    };
    const auto kPill = [this](ui::Node bar, std::string_view menu, std::optional<Icon> which, std::string_view text) {
        auto made = box(bar,
                        ui::Layout{.alignItems = ui::Align::Center,
                                   .gap = 6,
                                   .shrink = 0,
                                   .padding = {9, 8, 5, 5},
                                   .border = {1, 1, 1, 1}},
                        0);
        if (!made.has_value()) {
            return;
        }
        static_cast<void>(
            tree_->setLook(*made, ui::Look{.fill = 0x121317E6U, .borderColor = 0xFFFFFF14U, .radius = 5}));
        if (which.has_value()) {
            static_cast<void>(icon(*made, *which, look::kSoftInk, 13));
        }
        if (auto label = box(*made, ui::Layout{.shrink = 0}, 0); label.has_value()) {
            static_cast<void>(words(*label, text, look::kRowInk, look::kLabelSize));
            static_cast<void>(tree_->setInteraction(*label, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        static_cast<void>(icon(*made, Icon::ChevronDown, look::kFaintInk, 10));
        static_cast<void>(tree_->setRole(*made, ui::Role::Button));
        static_cast<void>(tree_->setName(*made, menu));
        held_.viewPills.emplace_back(*made, std::string{menu});
        menus_.emplace_back(*made, std::string{menu});
    };
    // While the game plays, not edited, it is the game's: only who has
    // its input, as the design's (D916).
    if (playing_.has_value() && !playing_->edited()) {
        // Nothing of Studio's over the game's own top corners, where its
        // HUD is likely to be: no mode palette, and the label below.
        if (tree_->contains(held_.modePalette)) {
            static_cast<void>(tree_->detach(held_.modePalette));
        }
        if (auto bar =
                kOverlay({.absolute = true, .x = ui::pixels(0), .y = ui::share(1), .anchorY = 1}, ui::Direction::Row);
            bar.has_value()) {
            held_.viewBar = *bar;
            if (auto label = box(*bar, ui::Layout{.shrink = 0, .padding = {9, 9, 5, 5}, .border = {1, 1, 1, 1}}, 0);
                label.has_value()) {
                static_cast<void>(
                    tree_->setLook(*label, ui::Look{.fill = 0x121317E6U, .borderColor = 0x35507FFFU, .radius = 5}));
                static_cast<void>(words(*label,
                                        players_ == 1 ? std::string{"Client 1 \u00B7 has input"}
                                                      : "Client 1 of " + std::to_string(players_) + " \u00B7 has input",
                                        look::kRowInk,
                                        look::kLabelSize));
            }
        }
        return;
    }
    if (tree_->contains(held_.modePalette) && !documentChosen()) {
        static_cast<void>(tree_->detach(held_.modePalette));
        static_cast<void>(tree_->attach(centerColumn_, held_.modePalette));
    }
    // The pills along the top, as the design's.
    if (auto bar =
            kOverlay({.absolute = true, .x = ui::pixels(0), .y = ui::pixels(look::kTabHeight + held_.prefabTall)},
                     ui::Direction::Row);
        bar.has_value()) {
        held_.viewBar = *bar;
        kPill(*bar, kProjectionPill, std::nullopt, held_.flat ? "Top" : "Perspective");
        const std::string_view kMode = shown_.viewMode == "unlit"     ? "Unlit"
                                       : shown_.viewMode == "normals" ? "Normals"
                                                                      : "Lit";
        kPill(*bar, kDrawPill, std::nullopt, kMode);
        std::string showing;
        for (const std::string& kOverlayName : shown_.overlays) {
            showing += (showing.empty() ? "" : ", ") + kOverlayName;
        }
        kPill(*bar, kShowPill, Icon::Eye, "Show: " + (showing.empty() ? std::string{"nothing"} : showing));
        kPill(*bar, kBookmarkPill, Icon::Bookmark, "Bookmarks");
    }
    if (auto bar = kOverlay(
            {.absolute = true, .x = ui::share(1), .y = ui::pixels(look::kTabHeight + held_.prefabTall), .anchorX = 1},
            ui::Direction::Row);
        bar.has_value()) {
        held_.splitBar = *bar;
        kPill(*bar, kSplitPill, Icon::Split, "Split");
    }
    // The axes' disc under the split, each end its axis's color.
    if (auto disc = kOverlay({.absolute = true,
                              .x = ui::share(1),
                              .y = ui::pixels(look::kTabHeight + held_.prefabTall + 40),
                              .anchorX = 1},
                             ui::Direction::Row);
        disc.has_value()) {
        held_.axisGizmo = *disc;
        static_cast<void>(tree_->setName(*disc, "Axes"));
        if (auto round = box(*disc, ui::Layout{.width = ui::pixels(64), .height = ui::pixels(64), .shrink = 0}, 0);
            round.has_value()) {
            static_cast<void>(
                tree_->setLook(*round, ui::Look{.fill = 0x0B0D10B0U, .borderColor = 0xFFFFFF14U, .radius = 32}));
            static_cast<void>(tree_->setLayout(
                *round,
                ui::Layout{.width = ui::pixels(64), .height = ui::pixels(64), .shrink = 0, .border = {1, 1, 1, 1}}));
            static_cast<void>(tree_->setInteraction(*round, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            constexpr std::array<std::uint32_t, 3> kInks = {look::kAxisX, look::kAxisY, look::kAxisZ};
            constexpr std::array<std::string_view, 3> kLetters = {"X", "Y", "Z"};
            for (std::size_t each = 0; each < 3; ++each) {
                auto end = box(*round,
                               ui::Layout{.width = ui::pixels(14),
                                          .height = ui::pixels(14),
                                          .justify = ui::Justify::Center,
                                          .alignItems = ui::Align::Center,
                                          .placement = {.absolute = true, .x = ui::pixels(25), .y = ui::pixels(25)}},
                               0);
                if (!end.has_value()) {
                    break;
                }
                static_cast<void>(tree_->setLook(*end, ui::Look{.fill = kInks[each], .radius = 7}));
                static_cast<void>(words(*end, kLetters[each], 0x0B0D10FFU, 9));
                held_.axisEnds[each] = *end;
            }
        }
        showAxes();
    }
    // How the view is flown, at its bottom left.
    if (auto hint =
            kOverlay({.absolute = true, .x = ui::pixels(0), .y = ui::share(1), .anchorY = 1}, ui::Direction::Row);
        hint.has_value()) {
        held_.flyHint = *hint;
        static_cast<void>(tree_->setInteraction(*hint, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        for (const auto& [kKey, kWhat] : {std::pair<std::string_view, std::string_view>{"RMB", "fly with WASD"},
                                          std::pair<std::string_view, std::string_view>{"F", "frame selection"}}) {
            auto made = box(*hint,
                            ui::Layout{.alignItems = ui::Align::Center,
                                       .gap = 6,
                                       .shrink = 0,
                                       .padding = {8, 9, 4, 4},
                                       .border = {1, 1, 1, 1}},
                            0);
            if (!made.has_value()) {
                break;
            }
            static_cast<void>(
                tree_->setLook(*made, ui::Look{.fill = 0x121317E6U, .borderColor = 0xFFFFFF14U, .radius = 5}));
            if (auto key = box(*made, ui::Layout{.shrink = 0, .padding = {4, 4, 1, 1}, .border = {1, 1, 1, 1}}, 0);
                key.has_value()) {
                static_cast<void>(tree_->setLook(*key, ui::Look{.borderColor = look::kRaisedEdge, .radius = 3}));
                static_cast<void>(monoWords(*key, kKey, look::kSoftInk, 10));
            }
            if (auto said = box(*made, ui::Layout{.shrink = 0}, 0); said.has_value()) {
                static_cast<void>(words(*said, kWhat, look::kSoftInk, look::kLabelSize));
            }
        }
    }
}

void ShellParticipant::showAxes() {
    // Each axis as the view sees it: right along the disc's width, up
    // along its height (D916).
    const auto kVector = [this](std::string_view name) -> std::optional<std::array<double, 3>> {
        const Value* kPart = view_.has_value() ? view_->find(name) : nullptr;
        if (kPart == nullptr || kPart->items().size() != 3) {
            return std::nullopt;
        }
        std::array<double, 3> made{};
        for (std::size_t each = 0; each < 3; ++each) {
            const Value& kItem = kPart->items()[each];
            made[each] = kItem.real().value_or(static_cast<double>(kItem.integer().value_or(0)));
        }
        return made;
    };
    const std::optional<std::array<double, 3>> kEye = kVector("eye");
    const std::optional<std::array<double, 3>> kTarget = kVector("target");
    std::array<double, 3> forward{-0.5, -0.5, -0.7};
    if (kEye.has_value() && kTarget.has_value()) {
        forward = {(*kTarget)[0] - (*kEye)[0], (*kTarget)[1] - (*kEye)[1], (*kTarget)[2] - (*kEye)[2]};
    }
    const auto kUnit = [](std::array<double, 3> v) {
        const double kLength = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return kLength > 1e-9 ? std::array<double, 3>{v[0] / kLength, v[1] / kLength, v[2] / kLength} : v;
    };
    forward = kUnit(forward);
    std::array<double, 3> right = kUnit({-forward[2], 0, forward[0]});
    if (std::abs(forward[1]) > 0.999) {
        right = {1, 0, 0};
    }
    const std::array<double, 3> kUp = {right[1] * forward[2] - right[2] * forward[1],
                                       right[2] * forward[0] - right[0] * forward[2],
                                       right[0] * forward[1] - right[1] * forward[0]};
    for (std::size_t each = 0; each < 3; ++each) {
        if (!tree_->contains(held_.axisEnds[each])) {
            continue;
        }
        const double kAcross = right[each];
        const double kHigh = kUp[each];
        static_cast<void>(
            tree_->setLayout(held_.axisEnds[each],
                             ui::Layout{.width = ui::pixels(14),
                                        .height = ui::pixels(14),
                                        .justify = ui::Justify::Center,
                                        .alignItems = ui::Align::Center,
                                        .placement = {.absolute = true,
                                                      .x = ui::pixels(static_cast<float>(25 + 20 * kAcross)),
                                                      .y = ui::pixels(static_cast<float>(25 - 20 * kHigh))}}));
    }
}

void ShellParticipant::showViewportNote() {
    // What the viewport shows while it has no picture (D922): the cook
    // the edit view waits for, the game starting, or that it stopped.
    std::string note;
    if (tree_->contains(viewport_) && (inViewport_.picture == nullptr || !playing_.has_value())) {
        note = cooking_.has_value()                          ? "Cooking the game to show it here..."
               : playing_.has_value() && playing_->running() ? "Starting the game..."
               : playing_.has_value() || held_.editStopped   ? "The game stopped. The Console says why."
               : scene_.empty() ? "This game makes its world in Kest. Press Play to run it here."
               // Joined, the scene is the host's, which Play shows as cooked
               // there (D905, D935); the edit view plays only this author's
               // own files.
               : held_.joined != nullptr ? "The scene is the host's: Play shows it as the host cooks it."
                                         : "";
    }
    // An empty scene's first steps, over its view (D923).
    if (note.empty() && tree_->contains(viewport_) && chosen(Listed::Scene) && entities_.empty() && !live_.on &&
        !(playing_.has_value() && !playing_->edited())) {
        note = "empty";
    }
    if (note == held_.viewportNoteText && (note.empty() || tree_->contains(held_.viewportNote))) {
        return;
    }
    held_.viewportNoteText = note;
    if (tree_->contains(held_.viewportNote)) {
        static_cast<void>(tree_->remove(held_.viewportNote));
    }
    held_.viewportNote = ui::Node{};
    if (note.empty()) {
        return;
    }
    if (note == "empty") {
        showEmptyScene();
        return;
    }
    auto made = box(
        centerColumn_,
        ui::Layout{
            .alignItems = ui::Align::Center,
            .gap = 8,
            .padding = {14, 14, 9, 9},
            .border = {1, 1, 1, 1},
            .placement = {.absolute = true, .x = ui::share(0.5), .y = ui::share(0.45), .anchorX = 0.5, .anchorY = 0.5}},
        0);
    if (!made.has_value()) {
        return;
    }
    held_.viewportNote = *made;
    viewRows_.push_back(*made);
    static_cast<void>(tree_->setLook(*made, ui::Look{.fill = 0x121317E6U, .borderColor = 0xFFFFFF14U, .radius = 6}));
    static_cast<void>(icon(*made, cooking_.has_value() ? Icon::Cook : Icon::Play, look::kLabelInk, 14));
    if (auto said = box(*made, ui::Layout{.shrink = 0}, 0); said.has_value()) {
        static_cast<void>(words(*said, note, look::kSoftInk, look::kBodySize));
    }
}

void ShellParticipant::showEmptyScene() {
    auto made = box(
        centerColumn_,
        ui::Layout{
            .width = ui::pixels(300),
            .direction = ui::Direction::Column,
            .gap = 4,
            .padding = {14, 14, 12, 12},
            .border = {1, 1, 1, 1},
            .placement = {.absolute = true, .x = ui::share(0.5), .y = ui::share(0.45), .anchorX = 0.5, .anchorY = 0.5}},
        0);
    if (!made.has_value()) {
        return;
    }
    held_.viewportNote = *made;
    viewRows_.push_back(*made);
    static_cast<void>(tree_->setLook(*made, ui::Look{.fill = 0x121317F0U, .borderColor = look::kEdge, .radius = 8}));
    if (auto title = box(*made, ui::Layout{.shrink = 0, .padding = {0, 0, 0, 6}}, 0); title.has_value()) {
        static_cast<void>(strongWords(*title, "An empty scene", look::kInk, 15));
    }
    held_.sceneStartButtons.clear();
    const std::vector<std::string> kParts = partCommands();
    for (const auto& [kCommand, kLabel, kWhich] :
         {std::tuple<std::string_view, std::string_view, Icon>{"New cube", "Add a cube", Icon::Plus},
          std::tuple<std::string_view, std::string_view, Icon>{"New lamp", "Add a lamp", Icon::Bulb},
          std::tuple<std::string_view, std::string_view, Icon>{"New empty", "Add an empty entity", Icon::Cube}}) {
        if (!std::ranges::contains(kParts, std::string{kCommand})) {
            continue;
        }
        if (auto button = textButton(*made, kWhich, kLabel, ButtonKind::Quiet); button.has_value()) {
            held_.sceneStartButtons.emplace_back(*button, std::string{kCommand});
        }
    }
    if (auto button = textButton(*made, Icon::Folder, "Import an asset", ButtonKind::Quiet); button.has_value()) {
        held_.sceneStartButtons.emplace_back(*button, std::string{"Import"});
    }
    if (auto said = box(*made, ui::Layout{.shrink = 0, .padding = {0, 0, 8, 0}}, 0); said.has_value()) {
        static_cast<void>(wrappedWords(*said,
                                       "Or write " + std::filesystem::path{scene_}.filename().string() +
                                           " in your editor; Studio shows it when you save.",
                                       look::kFaintInk,
                                       look::kLabelSize));
    }
}

void ShellParticipant::forwardInput() {
    if (inViewport_.forwarding == nullptr) {
        return;
    }
    // While the game plays in the viewport and shows there, its area is
    // the game's; else the window host acts on all of it. A menu, popup,
    // or dialog open over it takes what lands on it (D797: the View
    // menu's lower rows lie over the viewport).
    const bool kPlaysHere = playing_.has_value() && play_.has_value() && play_->viewport.has_value() &&
                            inViewport_.picture != nullptr && tree_->contains(viewport_) && !scene_.empty() &&
                            !popup_.has_value() && !dialog_.has_value() && !held_.exportDialog.has_value();
    if (!kPlaysHere) {
        inViewport_.forwarding->forwardOver(std::nullopt);
        static_cast<void>(inViewport_.forwarding->take());
        return;
    }
    // Only what shows of it: the viewport keeps its least height where
    // the center is shorter, running on under the dock, whose tabs a press
    // there is for (D901).
    const ui::Rect kAt = tree_->placedRectOf(viewport_);
    const ui::Rect kCenter = tree_->placedRectOf(centerColumn_);
    const float kTop = std::max(kAt.y, kCenter.y);
    const float kLeft = std::max(kAt.x, kCenter.x);
    const float kBottom = std::min(kAt.y + kAt.height, kCenter.y + kCenter.height);
    const float kRight = std::min(kAt.x + kAt.width, kCenter.x + kCenter.width);
    inViewport_.forwarding->forwardOver(
        std::array<float, 4>{kLeft, kTop, std::max(0.0F, kRight - kLeft), std::max(0.0F, kBottom - kTop)});
    // Studio's own controls over it keep their presses, and while it is
    // edited the keys are Studio's but while a press there flies (D916).
    std::vector<std::array<float, 4>> holes;
    std::vector<ui::Node> over{held_.modePalette, held_.axisGizmo, held_.viewportNote};
    for (const auto& [kPill, kMenu] : held_.viewPills) {
        over.push_back(kPill);
    }
    for (const ui::Node kOver : over) {
        if (tree_->contains(kOver)) {
            const ui::Rect kHole = tree_->placedRectOf(kOver);
            holes.push_back({kHole.x, kHole.y, kHole.width, kHole.height});
        }
    }
    inViewport_.forwarding->exceptOver(std::move(holes));
    inViewport_.forwarding->keysWhileHeld(playing_->edited());
    std::vector<world_tooling::WindowInput> events = inViewport_.forwarding->take();
    // At the picture's pixels, fewer than the viewport's where it would
    // pass the UI's uploads (D927) or the viewport grew since it started.
    if (kAt.width > 0 && kAt.height > 0) {
        const float kAcross = static_cast<float>(inViewport_.picture->levels[0].width) / kAt.width;
        const float kDown = static_cast<float>(inViewport_.picture->levels[0].height) / kAt.height;
        for (world_tooling::WindowInput& event : events) {
            event.x *= kAcross;
            event.y *= kDown;
        }
    }
    // In records of at most the most one carries.
    for (std::size_t from = 0; from < events.size(); from += world_tooling::kMostWindowInputs) {
        const std::size_t kTo = std::min(events.size(), from + world_tooling::kMostWindowInputs);
        const std::vector<world_tooling::WindowInput> kPart{events.begin() + static_cast<std::ptrdiff_t>(from),
                                                            events.begin() + static_cast<std::ptrdiff_t>(kTo)};
        inViewport_.inputsForwarded += kPart.size();
        const std::string kReply = ask(previewInputRecord(next(), scene_, world_tooling::windowInputsValue(kPart)));
        if (kReply.find("\"taken\":true") != std::string::npos) {
            ++inViewport_.inputsTaken;
        }
    }
}

} // namespace rawframe::studio
