#include "shell.h"

#include <algorithm>
#include <array>
#include <tuple>

namespace rawframe::studio {

namespace {

/// The targets SPEC-0051 gives the dialog, each by the word Studio shows
/// and the one `rawframe-export` takes.
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kTargets = {
    {{"desktop", "native"}, {"web", "web"}, {"android", "android"}, {"ios", "ios"}}};

/// A target as an author reads it: the desktop named for the system Studio
/// runs on, which it exports for (D947).
std::string_view shownTarget(std::string_view target) {
    if (target != "desktop") {
        return target;
    }
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

/// The last line of `text` with something on it.
std::string lastLine(std::string_view text) {
    const std::size_t kEnd = text.find_last_not_of(" \t\r\n");
    if (kEnd == std::string_view::npos) {
        return {};
    }
    const std::size_t kStart = text.find_last_of('\n', kEnd);
    return std::string{text.substr(kStart == std::string_view::npos ? 0 : kStart + 1,
                                   kEnd - (kStart == std::string_view::npos ? 0 : kStart + 1) + 1)};
}

} // namespace

std::filesystem::path ShellParticipant::exportOutput() const {
    if (!held_.exportOutput.empty()) {
        return held_.exportOutput;
    }
    // Beside the game's directory, by its name and the target, so the
    // game's own files never hold what is made of them.
    const std::filesystem::path kGame = description_.parent_path();
    return kGame.parent_path() / (kGame.filename().string() + "-" + std::string{shownTarget(held_.exportTarget)});
}

void ShellParticipant::openExport() {
    if (held_.exportDialog.has_value()) {
        closeExport();
        return;
    }
    closePopup();
    closePreferences();
    // As the design's Export dialog (D937): its heading and the tool that
    // does the work, a card for each target, where it goes and what it is
    // called, what the export said, and Cancel and Export at its foot.
    auto dialog =
        box(root_,
            ui::Layout{.width = ui::pixels(720),
                       .direction = ui::Direction::Column,
                       .border = {1, 1, 1, 1},
                       .placement = {.absolute = true, .x = ui::share(0.5F), .y = ui::share(0.12F), .anchorX = 0.5F}},
            0);
    if (!dialog.has_value()) {
        return;
    }
    held_.exportDialog = *dialog;
    static_cast<void>(tree_->setLook(*dialog,
                                     ui::Look{.fill = 0x1A1C21FFU,
                                              .borderColor = look::kRaisedEdge,
                                              .radius = 8,
                                              .outerShadow = {.color = 0x00000099U, .y = 10, .blur = 30}}));
    static_cast<void>(tree_->setRole(*dialog, ui::Role::Group));
    static_cast<void>(tree_->setName(*dialog, "Export the game"));
    held_.exportTargets.clear();
    held_.exportFields.clear();
    const std::string kGame = description_.parent_path().filename().string();
    if (auto head = box(*dialog,
                        ui::Layout{.height = ui::pixels(46),
                                   .alignItems = ui::Align::Center,
                                   .shrink = 0,
                                   .padding = {16, 16, 0, 0},
                                   .border = {0, 0, 0, 1}},
                        0);
        head.has_value()) {
        static_cast<void>(tree_->setLook(*head, ui::Look{.borderColor = look::kRaisedEdge}));
        if (auto title = box(*head, ui::Layout{.shrink = 0}, 0); title.has_value()) {
            static_cast<void>(strongWords(*title, "Export " + kGame, look::kInk, 15));
        }
        static_cast<void>(box(*head, ui::Layout{.grow = 1}, 0));
        if (auto tool = box(*head, ui::Layout{.shrink = 0}, 0); tool.has_value()) {
            static_cast<void>(monoWords(*tool, "rawframe-export", look::kFaintInk, look::kSmallSize));
        }
    }
    auto body = box(*dialog, ui::Layout{.direction = ui::Direction::Column, .gap = 6, .padding = {16, 16, 14, 14}}, 0);
    if (!body.has_value()) {
        return;
    }
    // Each target a card, the chosen one lit; the desktop's named for the
    // system Studio runs on, which it exports for.
#if defined(_WIN32)
    const std::array<std::pair<std::string, std::string>, 4> kCards = {{{"Windows", "a folder with " + kGame + ".exe"},
                                                                        {"Web", "a site to serve"},
                                                                        {"Android", "an APK, Vulkan 1.3"},
                                                                        {"iOS", "an app for the simulator"}}};
#elif defined(__APPLE__)
    const std::array<std::pair<std::string, std::string>, 4> kCards = {{{"macOS", "a folder with " + kGame},
                                                                        {"Web", "a site to serve"},
                                                                        {"Android", "an APK, Vulkan 1.3"},
                                                                        {"iOS", "an app for the simulator"}}};
#else
    const std::array<std::pair<std::string, std::string>, 4> kCards = {{{"Linux", "a folder with " + kGame},
                                                                        {"Web", "a site to serve"},
                                                                        {"Android", "an APK, Vulkan 1.3"},
                                                                        {"iOS", "an app for the simulator"}}};
#endif
    if (auto cards = box(*body, ui::Layout{.gap = 8, .shrink = 0, .margin = {0, 0, 0, 8}}, 0); cards.has_value()) {
        for (std::size_t each = 0; each < kTargets.size(); ++each) {
            const bool kChosenTarget = kTargets[each].first == held_.exportTarget;
            auto card = box(*cards,
                            ui::Layout{.width = ui::share(0.25F),
                                       .direction = ui::Direction::Column,
                                       .gap = 4,
                                       .shrink = 1,
                                       .padding = {12, 12, 10, 10},
                                       .border = {1, 1, 1, 1}},
                            0);
            if (!card.has_value()) {
                break;
            }
            static_cast<void>(tree_->setLook(*card,
                                             ui::Look{.fill = kChosenTarget ? 0x1B2A44FFU : 0x202228FFU,
                                                      .borderColor = kChosenTarget ? look::kAccent : look::kRaisedEdge,
                                                      .radius = 6}));
            static_cast<void>(tree_->setRole(*card, ui::Role::Button));
            static_cast<void>(tree_->setName(*card, std::string{kTargets[each].first}));
            for (const auto& [kLine, kStrong] : {std::pair<std::string_view, bool>{kCards[each].first, true},
                                                 std::pair<std::string_view, bool>{kCards[each].second, false}}) {
                if (auto said = box(*card, ui::Layout{.shrink = 0}, 0); said.has_value()) {
                    static_cast<void>(
                        tree_->setInteraction(*said, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                    static_cast<void>(kStrong ? strongWords(*said, kLine, look::kInk, look::kBodySize)
                                              : words(*said, kLine, look::kLabelInk, look::kSmallSize));
                }
            }
            held_.exportTargets.emplace_back(*card, std::string{kTargets[each].first});
        }
    }
    const bool kPhone = held_.exportTarget == "android" || held_.exportTarget == "ios";
    const std::array<std::tuple<std::string, std::string, std::string_view>, 3> kFields = {
        {{"output", exportOutput().string(), "Into"},
         {"title", held_.exportTitle.empty() ? kGame : held_.exportTitle, "Title"},
         {"address", held_.exportAddress, "Server address"}}};
    for (const auto& [kSetting, kValue, kLabel] : kFields) {
        if (kSetting == "address" && !kPhone) {
            continue;
        }
        const bool kGiven = kSetting == "output"  ? !held_.exportOutput.empty()
                            : kSetting == "title" ? !held_.exportTitle.empty()
                                                  : !held_.exportAddress.empty();
        if (auto line = fieldRow(kSetting, kValue, *body, kGiven, kLabel); line.has_value()) {
            held_.exportFields.emplace_back(line->second, kSetting);
        }
    }
    if (kPhone) {
        if (auto note = box(*body, ui::Layout{.shrink = 0, .padding = {0, 0, 0, 2}}, 0); note.has_value()) {
            static_cast<void>(words(
                *note, "The address a phone reaches this machine's server by.", look::kFaintInk, look::kSmallSize));
        }
    }
    // What the export said last, and while it runs a bar that says so.
    if (!held_.exportSaid.empty()) {
        if (auto said = box(*body, ui::Layout{.shrink = 0, .margin = {0, 0, 8, 0}}, 0); said.has_value()) {
            static_cast<void>(wrappedWords(*said, held_.exportSaid, look::kSoftInk, look::kLabelSize));
        }
    }
    if (held_.exporting.has_value()) {
        if (auto track = box(*body, ui::Layout{.height = ui::pixels(4), .shrink = 0}, 0x2A2D35FFU); track.has_value()) {
            static_cast<void>(box(*track, ui::Layout{.width = ui::share(0.6F)}, look::kAccent));
        }
    }
    if (auto foot = box(
            *dialog, ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0, .padding = {16, 16, 0, 14}}, 0);
        foot.has_value()) {
        static_cast<void>(box(*foot, ui::Layout{.grow = 1}, 0));
        if (auto close =
                textButton(*foot, std::nullopt, held_.exporting.has_value() ? "Close" : "Cancel", ButtonKind::Quiet);
            close.has_value()) {
            held_.exportClose = *close;
            static_cast<void>(tree_->setName(*close, "Close"));
        }
        if (auto exported = textButton(
                *foot, std::nullopt, held_.exporting.has_value() ? "Exporting" : "Export", ButtonKind::Primary);
            exported.has_value()) {
            held_.exportPress = *exported;
        }
    }
}

void ShellParticipant::closeExport() {
    if (!held_.exportDialog.has_value()) {
        return;
    }
    endEdit();
    if (tree_->contains(*held_.exportDialog)) {
        static_cast<void>(tree_->remove(*held_.exportDialog));
    }
    held_.exportDialog.reset();
    held_.exportTargets.clear();
    held_.exportFields.clear();
}

bool ShellParticipant::pressExport(ui::Node node) {
    if (!held_.exportDialog.has_value()) {
        return false;
    }
    if (node == held_.exportClose) {
        closeExport();
        return true;
    }
    if (node == held_.exportPress) {
        startExport();
        return true;
    }
    if (const auto kTarget = std::ranges::find(held_.exportTargets, node, &std::pair<ui::Node, std::string>::first);
        kTarget != held_.exportTargets.end()) {
        held_.exportTarget = kTarget->second;
        // What the last export said was of the target left (D948).
        held_.exportSaid.clear();
        say("exports for " + std::string{shownTarget(held_.exportTarget)});
        closeExport();
        openExport();
        return true;
    }
    if (const auto kSetting = std::ranges::find(held_.exportFields, node, &std::pair<ui::Node, std::string>::first);
        kSetting != held_.exportFields.end()) {
        beginEdit(FieldRow{.value = kSetting->first, .role = FieldRow::Role::ExportSetting, .field = kSetting->second});
        return true;
    }
    return false;
}

void ShellParticipant::setExport(const std::string& field, const std::string& text) {
    // What is typed as its default is no choice of the author's.
    if (field == "output") {
        // The default is what an output not given would be.
        held_.exportOutput.clear();
        held_.exportOutput = text == exportOutput().string() ? std::string{} : text;
    } else if (field == "title") {
        held_.exportTitle = text == description_.parent_path().filename().string() ? std::string{} : text;
    } else if (field == "address") {
        held_.exportAddress = text;
    }
    say("the export's " + field + " set");
    closeExport();
    openExport();
}

void ShellParticipant::startExport() {
    if (held_.exporting.has_value()) {
        say("an export is running");
        return;
    }
    // Export is a headless operation (ADR-0042): the dialog only tells
    // `rawframe-export` what the author chose, and the tools it runs are
    // the ones beside it.
    const std::filesystem::path kOutput = exportOutput();
    const bool kPhone = held_.exportTarget == "android" || held_.exportTarget == "ios";
    std::error_code error;
    const auto kSay = [this](std::string said) {
        held_.exportSaid = std::move(said);
        say(held_.exportSaid);
        closeExport();
        openExport();
    };
    if (std::filesystem::exists(kOutput, error) && !std::filesystem::is_empty(kOutput, error)) {
        kSay(kOutput.string() + " holds files already: export into a new or empty directory");
        return;
    }
    if (kPhone && held_.exportAddress.empty()) {
        kSay("a phone's export needs the address it reaches this machine's server by");
        return;
    }
    const std::optional<std::filesystem::path> kOwn = ownDirectory();
    if (!kOwn.has_value()) {
        kSay("Studio has no directory of its own for the export's output");
        return;
    }
    const auto kTarget =
        std::ranges::find(kTargets, held_.exportTarget, &std::pair<std::string_view, std::string_view>::first);
    std::vector<std::string> arguments = {description_.parent_path().string(),
                                          kOutput.string(),
                                          "--game",
                                          description_.filename().string(),
                                          "--target",
                                          std::string{kTarget != kTargets.end() ? kTarget->second : "native"},
                                          "--tools",
                                          exportTool_.parent_path().string()};
    if (!held_.exportTitle.empty()) {
        arguments.insert(arguments.end(), {"--title", held_.exportTitle});
    }
    if (kPhone) {
        arguments.insert(arguments.end(), {"--address", held_.exportAddress});
    }
    auto started =
        process::Child::start({.program = exportTool_, .arguments = arguments, .output = *kOwn / "export.out"});
    if (!started.has_value()) {
        kSay("the export did not start: " + std::string{started.error().description()});
        return;
    }
    held_.exporting = std::move(*started);
    held_.exportingTo = kOutput;
    kSay("exporting for " + std::string{shownTarget(held_.exportTarget)} + " to " + kOutput.string());
}

void ShellParticipant::pollExport() {
    if (!held_.exporting.has_value()) {
        return;
    }
    const std::optional<int> kCode = held_.exporting->exited();
    if (!kCode.has_value()) {
        return;
    }
    held_.exporting.reset();
    const std::string kOutput = authoring_session::readFile(held_.own / "export.out").value_or("");
    std::error_code error;
    const bool kReceipt = std::filesystem::exists(held_.exportingTo / "export.receipt", error);
    if (*kCode == 0) {
        ++held_.exports;
        held_.exportSaid =
            "exported for " + std::string{shownTarget(held_.exportTarget)} + " to " + held_.exportingTo.string();
    } else {
        held_.exportSaid = "the export failed: " + lastLine(kOutput);
    }
    say(held_.exportSaid);
    emitter_.log(*kCode == 0 ? diagnostics::Severity::Info : diagnostics::Severity::Warning,
                 kExported,
                 *kCode == 0 ? "the game exported" : "the game's export failed",
                 {diagnostics::field("target", held_.exportTarget),
                  diagnostics::field("output", held_.exportingTo.string()),
                  diagnostics::field("code", static_cast<std::int64_t>(*kCode)),
                  diagnostics::field("receipt", kReceipt),
                  diagnostics::field("exports", held_.exports)});
    if (held_.exportDialog.has_value()) {
        closeExport();
        openExport();
    }
}

} // namespace rawframe::studio
