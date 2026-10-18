#include "shell.h"

#include <algorithm>
#include <array>

namespace rawframe::studio {

namespace {

/// The targets SPEC-0051 gives the dialog, each by the word Studio shows
/// and the one `rawframe-export` takes.
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kTargets = {
    {{"desktop", "native"}, {"web", "web"}, {"android", "android"}, {"ios", "ios"}}};

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
    return kGame.parent_path() / (kGame.filename().string() + "-" + held_.exportTarget);
}

void ShellParticipant::openExport() {
    if (held_.exportDialog.has_value()) {
        closeExport();
        return;
    }
    closePopup();
    closePreferences();
    auto dialog =
        box(root_,
            ui::Layout{.width = ui::pixels(560),
                       .direction = ui::Direction::Column,
                       .gap = 4,
                       .padding = {12, 12, 12, 12},
                       .placement = {.absolute = true, .x = ui::share(0.5F), .y = ui::share(0.1F), .anchorX = 0.5F}},
            kHeaderFill);
    if (!dialog.has_value()) {
        return;
    }
    held_.exportDialog = *dialog;
    static_cast<void>(tree_->setRole(*dialog, ui::Role::Group));
    static_cast<void>(tree_->setName(*dialog, "Export the game"));
    held_.exportTargets.clear();
    held_.exportFields.clear();
    if (auto close = button(*dialog, "Close"); close.has_value()) {
        held_.exportClose = *close;
    }
    // Each target a press, the chosen one bright; then what the export is
    // told, a phone's address only for a phone.
    static_cast<void>(row(*dialog, "Target", kQuiet, kHeaderFill));
    auto targets = box(*dialog, ui::Layout{.height = ui::pixels(26), .gap = 4, .shrink = 0}, 0);
    for (const auto& [kWord, kTarget] : kTargets) {
        auto pressed = targets.has_value() ? box(*targets, ui::Layout{.padding = {10, 4, 10, 4}}, kRow)
                                           : std::unexpected<result::Error>{targets.error().clone()};
        if (!pressed.has_value()) {
            break;
        }
        static_cast<void>(pressWords(*pressed, kWord, kWord == held_.exportTarget ? kText : kQuiet, 13));
        held_.exportTargets.emplace_back(*pressed, std::string{kWord});
    }
    const bool kPhone = held_.exportTarget == "android" || held_.exportTarget == "ios";
    const std::array<std::pair<std::string, std::string>, 3> kFields = {
        {{"output", exportOutput().string()},
         {"title", held_.exportTitle.empty() ? description_.parent_path().filename().string() : held_.exportTitle},
         {"address", held_.exportAddress}}};
    for (const auto& [kSetting, kValue] : kFields) {
        if (kSetting == "address" && !kPhone) {
            continue;
        }
        const bool kGiven = kSetting == "output"  ? !held_.exportOutput.empty()
                            : kSetting == "title" ? !held_.exportTitle.empty()
                                                  : !held_.exportAddress.empty();
        if (auto line = fieldRow(kSetting, kValue, *dialog, kGiven); line.has_value()) {
            held_.exportFields.emplace_back(line->second, kSetting);
        }
    }
    if (kPhone) {
        static_cast<void>(row(*dialog, "the address a phone reaches this machine's server by", kQuiet, kHeaderFill));
    }
    if (auto exported = button(*dialog, held_.exporting.has_value() ? "Exporting" : "Export"); exported.has_value()) {
        held_.exportPress = *exported;
    }
    if (!held_.exportSaid.empty()) {
        static_cast<void>(row(*dialog, held_.exportSaid, kQuiet, kHeaderFill));
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
        say("exports for " + held_.exportTarget);
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
    kSay("exporting for " + held_.exportTarget + " to " + kOutput.string());
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
        held_.exportSaid = "exported for " + held_.exportTarget + " to " + held_.exportingTo.string();
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
