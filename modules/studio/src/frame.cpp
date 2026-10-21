#include "shell.h"

#include <algorithm>
#include <span>

// The window's frame as the screen design draws it (D915): the title and
// menu row, the toolbar, the docks' tab strips, and the status bar, from
// the design's parts: icon buttons, buttons, separators, and tabs.

namespace rawframe::studio {

namespace {

constexpr float kIconButtonSide = 28;
constexpr float kButtonHeight = 26;

} // namespace

result::Result<ui::Node> ShellParticipant::iconButton(ui::Node parent, Icon which, std::string_view name, bool on) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kButton,
                        box(parent,
                            ui::Layout{.width = ui::pixels(kIconButtonSide),
                                       .height = ui::pixels(kIconButtonSide),
                                       .justify = ui::Justify::Center,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0,
                                       .border = {1, 1, 1, 1}},
                            0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kIcon, icon(kButton, which, on ? look::kInk : look::kSoftInk));
    RAWFRAME_TRY(tree_->setRole(kButton, ui::Role::Button));
    RAWFRAME_TRY(tree_->setName(kButton, name));
    held_.buttonParts[tree_->keyOf(kButton)] =
        ButtonParts{.icon = kIcon, .which = which, .kind = ButtonKind::Icon, .text = std::string{name}};
    setIconButton(kButton, on);
    return kButton;
}

void ShellParticipant::setIconButton(ui::Node button, bool on, bool enabled) {
    static_cast<void>(tree_->setLook(
        button, ui::Look{.fill = on ? look::kSelected : 0, .borderColor = on ? look::kSelectedEdge : 0, .radius = 5}));
    const auto kParts = held_.buttonParts.find(tree_->keyOf(button));
    if (kParts != held_.buttonParts.end() && kParts->second.which.has_value()) {
        static_cast<void>(tree_->setLook(kParts->second.icon,
                                         ui::Look{.image = look::iconImage(*kParts->second.which),
                                                  .imageTint = !enabled ? look::kOffInk
                                                               : on     ? look::kInk
                                                                        : look::kSoftInk}));
    }
}

result::Result<ui::Node>
ShellParticipant::textButton(ui::Node parent, std::optional<Icon> which, std::string_view text, ButtonKind kind) {
    const bool kPrimary = kind == ButtonKind::Primary;
    const bool kQuietOne = kind == ButtonKind::Quiet;
    RAWFRAME_TRY_ASSIGN(const ui::Node kButton,
                        box(parent,
                            ui::Layout{.height = ui::pixels(kButtonHeight),
                                       .alignItems = ui::Align::Center,
                                       .gap = 6,
                                       .shrink = 0,
                                       .padding = {10, 10, 0, 0},
                                       .border = {1, 1, 1, 1}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kButton,
                                ui::Look{.fill = kPrimary    ? look::kAccent
                                                 : kQuietOne ? 0
                                                             : look::kRaisedFill,
                                         .borderColor = kPrimary    ? look::kAccent
                                                        : kQuietOne ? 0
                                                                    : look::kRaisedEdge,
                                         .radius = 5}));
    const std::uint32_t kInkOf = kPrimary ? look::kOnAccent : kQuietOne ? look::kSoftInk : look::kInk;
    ButtonParts parts{.which = which, .kind = kind, .text = std::string{text}};
    if (which.has_value()) {
        RAWFRAME_TRY_ASSIGN(parts.icon, icon(kButton, *which, kInkOf, 14));
    }
    RAWFRAME_TRY_ASSIGN(parts.label, box(kButton, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(kPrimary ? strongWords(parts.label, text, kInkOf) : words(parts.label, text, kInkOf, look::kBodySize));
    RAWFRAME_TRY(tree_->setInteraction(parts.label, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    RAWFRAME_TRY(tree_->setRole(kButton, ui::Role::Button));
    RAWFRAME_TRY(tree_->setName(kButton, text));
    held_.buttonParts[tree_->keyOf(kButton)] = std::move(parts);
    return kButton;
}

void ShellParticipant::relabel(ui::Node button, std::string_view text, std::optional<Icon> which) {
    const auto kFound = held_.buttonParts.find(tree_->keyOf(button));
    if (kFound == held_.buttonParts.end() || !tree_->contains(kFound->second.label)) {
        static_cast<void>(pressWords(button, text, kText, look::kBodySize));
        return;
    }
    ButtonParts& parts = kFound->second;
    const bool kPrimary = parts.kind == ButtonKind::Primary;
    // A primary button that stops is drawn red, as the design's Stop (D916).
    const bool kStops = kPrimary && which == Icon::Stop;
    if (kPrimary) {
        static_cast<void>(tree_->setLook(button,
                                         ui::Look{.fill = kStops ? 0x5A2027FFU : look::kAccent,
                                                  .borderColor = kStops ? 0x8A3540FFU : look::kAccent,
                                                  .radius = 5}));
    }
    const std::uint32_t kInkOf = kStops                            ? 0xFFD9D9FFU
                                 : kPrimary                        ? look::kOnAccent
                                 : parts.kind == ButtonKind::Quiet ? look::kSoftInk
                                                                   : look::kInk;
    static_cast<void>(kPrimary ? strongWords(parts.label, text, kInkOf)
                               : words(parts.label, text, kInkOf, look::kBodySize));
    if (which.has_value() && tree_->contains(parts.icon)) {
        parts.which = which;
        static_cast<void>(tree_->setLook(
            parts.icon, ui::Look{.image = look::iconImage(*which), .imageTint = kStops ? 0xFF6B6BFFU : kInkOf}));
    }
    parts.text = std::string{text};
    static_cast<void>(tree_->setName(button, text));
}

result::Status ShellParticipant::separator(ui::Node parent) {
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kLine,
        box(parent,
            ui::Layout{.width = ui::pixels(1), .height = ui::pixels(20), .shrink = 0, .margin = {6, 6, 0, 0}},
            look::kEdge));
    RAWFRAME_TRY(tree_->setLook(kLine, ui::Look{.fill = look::kEdge}));
    return {};
}

result::Result<ui::Node> ShellParticipant::tab(ui::Node strip, std::string_view text, bool on) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kTab,
                        box(strip, ui::Layout{.shrink = 0, .padding = {12, 12, 7, 6}, .border = {0, 0, 0, 2}}, 0));
    RAWFRAME_TRY(pressWords(kTab, text, on ? look::kInk : look::kLabelInk, look::kBodySize));
    held_.buttonParts[tree_->keyOf(kTab)] = ButtonParts{.kind = ButtonKind::Tab, .text = std::string{text}};
    setTab(kTab, on);
    return kTab;
}

void ShellParticipant::setTab(ui::Node tab, bool on) {
    static_cast<void>(tree_->setLook(tab, ui::Look{.fill = 0, .borderColor = on ? look::kAccent : 0}));
    const auto kFound = held_.buttonParts.find(tree_->keyOf(tab));
    if (kFound != held_.buttonParts.end()) {
        static_cast<void>(words(tab, kFound->second.text, on ? look::kInk : look::kLabelInk, look::kBodySize));
    }
}

result::Result<ui::Node> ShellParticipant::tabStrip(ui::Node parent) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kStrip,
                        box(parent,
                            ui::Layout{.height = ui::pixels(look::kTabHeight),
                                       .alignItems = ui::Align::End,
                                       .shrink = 0,
                                       .border = {0, 0, 0, 1}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kStrip, ui::Look{.fill = 0, .borderColor = look::kLine}));
    return kStrip;
}

result::Result<ui::Node> ShellParticipant::titleRow(ui::Node root) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitleBar,
                        box(root,
                            ui::Layout{.height = ui::pixels(look::kTitleHeight),
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0,
                                       .padding = {10, 10, 0, 0},
                                       .border = {0, 0, 0, 1}},
                            look::kTitleFill));
    RAWFRAME_TRY(tree_->setLook(kTitleBar, ui::Look{.fill = look::kTitleFill, .borderColor = look::kLine}));
    // The mark: the app icon's blue square and its four bars.
    RAWFRAME_TRY_ASSIGN(const ui::Node kMark,
                        box(kTitleBar,
                            ui::Layout{.width = ui::pixels(20),
                                       .height = ui::pixels(20),
                                       .justify = ui::Justify::Center,
                                       .alignItems = ui::Align::End,
                                       .gap = 2,
                                       .shrink = 0,
                                       .padding = {4, 4, 4, 4},
                                       .margin = {0, 6, 0, 0}},
                            look::kAccent));
    for (const float kBar : {7.0F, 12.0F, 9.0F}) {
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kBarNode,
            box(kMark, ui::Layout{.width = ui::pixels(2.5F), .height = ui::pixels(kBar), .shrink = 0}, 0));
        RAWFRAME_TRY(tree_->setLook(kBarNode, ui::Look{.fill = 0xFFFFFFFFU, .radius = 1}));
    }
    RAWFRAME_TRY(tree_->setName(kMark, "Rawframe"));
    return kTitleBar;
}

namespace {

/// A snap's number as the toolbar shows it: a whole one bare, else to its
/// hundredths.
std::string snapNumber(double value) {
    if (value == static_cast<double>(static_cast<std::int64_t>(value))) {
        return std::to_string(static_cast<std::int64_t>(value));
    }
    std::string text = std::to_string(value);
    while (text.ends_with('0')) {
        text.pop_back();
    }
    return text;
}

} // namespace

result::Status ShellParticipant::toolbar(ui::Node root) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kBar,
                        box(root,
                            ui::Layout{.height = ui::pixels(look::kToolbarHeight),
                                       .alignItems = ui::Align::Center,
                                       .gap = 4,
                                       .shrink = 0,
                                       .padding = {10, 10, 0, 0},
                                       .border = {0, 0, 0, 1}},
                            look::kBarFill));
    RAWFRAME_TRY(tree_->setLook(kBar, ui::Look{.fill = look::kBarFill, .borderColor = look::kLine}));
    const bool kOpen = gameOpen() && !broken_.has_value();
    if (kOpen) {
        // The tools a press in the viewport uses: choose, move, turn, scale.
        constexpr std::array<std::pair<Icon, std::string_view>, 4> kTools = {{{Icon::Select, "Select (Q)"},
                                                                              {Icon::Move, "Move (W)"},
                                                                              {Icon::Rotate, "Rotate (E)"},
                                                                              {Icon::Scale, "Scale (R)"}}};
        for (std::size_t each = 0; each < kTools.size(); ++each) {
            RAWFRAME_TRY_ASSIGN(held_.tools[each],
                                iconButton(kBar, kTools[each].first, kTools[each].second, each == held_.tool));
        }
        RAWFRAME_TRY(separator(kBar));
        RAWFRAME_TRY_ASSIGN(held_.worldButton,
                            textButton(kBar, Icon::World, held_.local ? "Local" : "World", ButtonKind::Quiet));
        // Read as what it switches, not as the World tab (D925).
        RAWFRAME_TRY(tree_->setName(held_.worldButton, held_.local ? "Handles: Local" : "Handles: World"));
        RAWFRAME_TRY(separator(kBar));
        // The snaps, a press stepping each through its values.
        RAWFRAME_TRY(icon(kBar, Icon::Grid, look::kLabelInk, 14));
        RAWFRAME_TRY_ASSIGN(held_.moveSnap, textButton(kBar, std::nullopt, snapNumber(snap_.move), ButtonKind::Normal));
        RAWFRAME_TRY(icon(kBar, Icon::Angle, look::kLabelInk, 14));
        RAWFRAME_TRY_ASSIGN(held_.turnSnap,
                            textButton(kBar, std::nullopt, snapNumber(snap_.turn) + "\u00B0", ButtonKind::Normal));
        RAWFRAME_TRY(separator(kBar));
        RAWFRAME_TRY_ASSIGN(held_.threeD, textButton(kBar, std::nullopt, "3D", ButtonKind::Quiet));
        RAWFRAME_TRY_ASSIGN(held_.twoD, textButton(kBar, std::nullopt, "2D", ButtonKind::Quiet));
    }
    RAWFRAME_TRY(box(kBar, ui::Layout{.grow = 1}, 0));
    // Cook and Play in the middle, Play the accent's (D915). The start
    // screen has no game to cook, play, or undo in (D781); a host joined
    // cooks on its own machine (D885), its cook brought here to be played
    // (D905).
    if (!gameOpen()) {
        menus_.clear();
    } else if (!cookTool_.empty() || held_.joined != nullptr) {
        RAWFRAME_TRY_ASSIGN(cookNode_, textButton(kBar, Icon::Cook, cooking_.has_value() ? "Stop" : "Cook"));
    }
    if ((session_ != nullptr || held_.joined != nullptr) && play_.has_value()) {
        const bool kPlaying = playing_.has_value() && !playing_->edited();
        RAWFRAME_TRY_ASSIGN(
            playNode_,
            textButton(kBar, kPlaying ? Icon::Stop : Icon::Play, kPlaying ? "Stop" : "Play", ButtonKind::Primary));
        RAWFRAME_TRY(tree_->setLayout(playNode_,
                                      ui::Layout{.height = ui::pixels(26),
                                                 .alignItems = ui::Align::Center,
                                                 .gap = 6,
                                                 .shrink = 0,
                                                 .padding = {10, 12, 0, 0},
                                                 .margin = {6, 0, 0, 0},
                                                 .border = {1, 1, 1, 1}}));
        if (kPlaying) {
            relabel(playNode_, "Stop", Icon::Stop);
        }
        RAWFRAME_TRY_ASSIGN(
            held_.playersButton,
            textButton(kBar,
                       std::nullopt,
                       players_ == 1 ? "Server + 1 client" : "Server + " + std::to_string(players_) + " clients"));
        RAWFRAME_TRY_ASSIGN(const ui::Node kPause, iconButton(kBar, Icon::Pause, "Pause"));
        RAWFRAME_TRY_ASSIGN(const ui::Node kStep, iconButton(kBar, Icon::Step, "Step"));
        held_.commandButtons.emplace_back(kPause, "Pause");
        held_.commandButtons.emplace_back(kStep, "Step");
    }
    RAWFRAME_TRY(box(kBar, ui::Layout{.grow = 1}, 0));
    if (gameOpen()) {
        RAWFRAME_TRY_ASSIGN(undoNode_, iconButton(kBar, Icon::Undo, "Undo"));
        RAWFRAME_TRY_ASSIGN(redoNode_, iconButton(kBar, Icon::Redo, "Redo"));
        RAWFRAME_TRY(showHistory());
        RAWFRAME_TRY(separator(kBar));
    }
    // The layout chooser (SPEC-0051 Part 1, D911), the layouts' commands
    // under it.
    held_.layoutChooser = ui::Node{};
    if (gameOpen()) {
        RAWFRAME_TRY_ASSIGN(held_.layoutChooser, textButton(kBar, Icon::Layout, kLayoutChooser, ButtonKind::Quiet));
    }
    showToolbar();
    return {};
}

void ShellParticipant::showToolbar() {
    for (std::size_t each = 0; each < held_.tools.size(); ++each) {
        if (tree_->contains(held_.tools[each])) {
            setIconButton(held_.tools[each], each == held_.tool);
        }
    }
    const bool kFlat = view_.has_value() && [this] {
        const Value* kEye = view_->find("eye");
        const Value* kTarget = view_->find("target");
        return kEye != nullptr && kTarget != nullptr && kEye->items().size() == 3 && kTarget->items().size() == 3 &&
               kEye->items()[0].real() == kTarget->items()[0].real() &&
               kEye->items()[1].real() == kTarget->items()[1].real();
    }();
    for (const auto& [kNode, kOn] : {std::pair{held_.threeD, !kFlat}, std::pair{held_.twoD, kFlat}}) {
        if (tree_->contains(kNode)) {
            static_cast<void>(tree_->setLook(kNode,
                                             ui::Look{.fill = kOn ? look::kSelected : 0,
                                                      .borderColor = kOn ? look::kSelectedEdge : 0,
                                                      .radius = 5}));
        }
    }
    if (tree_->contains(held_.moveSnap)) {
        relabel(held_.moveSnap, snapNumber(snap_.move));
        relabel(held_.turnSnap, snapNumber(snap_.turn) + "\u00B0");
    }
}

result::Status ShellParticipant::statusBar(ui::Node root) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kBar,
                        box(root,
                            ui::Layout{.height = ui::pixels(look::kStatusHeight),
                                       .alignItems = ui::Align::Center,
                                       .gap = 14,
                                       .shrink = 0,
                                       .padding = {10, 10, 0, 0},
                                       .border = {0, 0, 1, 0}},
                            look::kBarFill));
    RAWFRAME_TRY(tree_->setLook(kBar, ui::Look{.fill = look::kBarFill, .borderColor = look::kLine}));
    RAWFRAME_TRY_ASSIGN(statusNode_, box(kBar, ui::Layout{}, 0));
    RAWFRAME_TRY(box(kBar, ui::Layout{.grow = 1}, 0));
    RAWFRAME_TRY_ASSIGN(held_.statusFigures, box(kBar, ui::Layout{.alignItems = ui::Align::Center, .gap = 14}, 0));
    // Joined to a host, where (D935).
    if (held_.joined != nullptr) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kJoinedAt,
                            box(kBar, ui::Layout{.alignItems = ui::Align::Center, .gap = 6, .shrink = 0}, 0));
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kDot,
            box(kJoinedAt, ui::Layout{.width = ui::pixels(7), .height = ui::pixels(7), .shrink = 0}, 0));
        RAWFRAME_TRY(tree_->setLook(kDot, ui::Look{.fill = 0x3DBB6BFFU, .radius = 3.5F}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kWhere, box(kJoinedAt, ui::Layout{.shrink = 0}, 0));
        RAWFRAME_TRY(words(kWhere, "Joined " + held_.joinEndpoint + " as " + held_.joinedAs, look::kLabelInk, 12));
    }
    // Then, at its end, the game's branch, where it is under version
    // control (D871).
    RAWFRAME_TRY_ASSIGN(held_.branchNode, box(kBar, ui::Layout{}, 0));
    return {};
}

bool ShellParticipant::pressFrame(ui::Node node) {
    if (node == ui::Node{}) {
        return false;
    }
    if (const auto kTool = std::ranges::find(held_.tools, node); kTool != held_.tools.end()) {
        endEdit();
        held_.tool = static_cast<std::uint8_t>(kTool - held_.tools.begin());
        showToolbar();
        // The mark shows the new tool's handles (D974).
        markChosen();
        constexpr std::array<std::string_view, 4> kToolNames = {"select", "move", "rotate", "scale"};
        say(std::string{kToolNames[held_.tool]} + " tool");
        return true;
    }
    if (node == held_.worldButton) {
        held_.local = !held_.local;
        relabel(held_.worldButton, held_.local ? "Local" : "World");
        static_cast<void>(tree_->setName(held_.worldButton, held_.local ? "Handles: Local" : "Handles: World"));
        say(held_.local ? "handles along the entity's own axes" : "handles along the World's axes");
        return true;
    }
    if (node == held_.moveSnap || node == held_.turnSnap) {
        // Each step to the next, past the last to none.
        constexpr std::array<double, 6> kMoves = {0, 0.1, 0.25, 0.5, 1, 2};
        constexpr std::array<double, 5> kTurns = {0, 5, 15, 45, 90};
        const auto kNext = [](std::span<const double> steps, double now) {
            const auto kAt = std::ranges::upper_bound(steps, now);
            return kAt == steps.end() ? steps.front() : *kAt;
        };
        if (node == held_.moveSnap) {
            snap_.move = kNext(kMoves, snap_.move);
        } else {
            snap_.turn = kNext(kTurns, snap_.turn);
        }
        showToolbar();
        showViewText();
        say(snap_.move == 0 && snap_.turn == 0 ? "no snap" : "drags snap to " + snapText(snap_));
        return true;
    }
    if (node == held_.threeD || node == held_.twoD) {
        static_cast<void>(runCommand(node == held_.twoD ? "2D view" : "3D view"));
        showToolbar();
        return true;
    }
    if (node == held_.playersButton) {
        players_ = players_ % 4 + 1;
        relabel(held_.playersButton,
                players_ == 1 ? "Server + 1 client" : "Server + " + std::to_string(players_) + " clients");
        say(std::to_string(players_) + (players_ == 1 ? " player" : " players") +
            (playing_.has_value() && !playing_->edited() ? " from the next play" : ""));
        return true;
    }
    if (const auto kCommand = std::ranges::find(held_.commandButtons, node, &std::pair<ui::Node, std::string>::first);
        kCommand != held_.commandButtons.end()) {
        const std::string kName = kCommand->second;
        static_cast<void>(runCommand(kName));
        return true;
    }
    return false;
}

} // namespace rawframe::studio
