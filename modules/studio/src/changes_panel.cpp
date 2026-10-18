#include "shell.h"

#include <cstdlib>

namespace rawframe::studio {

namespace {

/// The most lines of a diff the panel shows.
constexpr std::size_t kDiffLinesShown = 200;

/// `text` without the line ends and spaces around it.
std::string trimmed(std::string_view text) {
    const std::size_t kStart = text.find_first_not_of(" \t\r\n");
    if (kStart == std::string_view::npos) {
        return {};
    }
    return std::string{text.substr(kStart, text.find_last_not_of(" \t\r\n") - kStart + 1)};
}

std::string firstLine(std::string_view text) {
    const std::string kTrimmed = trimmed(text);
    return kTrimmed.substr(0, kTrimmed.find('\n'));
}

} // namespace

std::optional<std::filesystem::path> ShellParticipant::ownDirectory() {
    // A directory of Studio's own, made afresh and closed to other users,
    // never a name in the shared temporary directory another may have made
    // first; the tools Studio runs write their output there.
    if (held_.own.empty()) {
        std::error_code error;
        const std::filesystem::path kDirectory =
            std::filesystem::temp_directory_path(error) / ("rawframe-studio-" + mintedHexIdentity());
        if (error || !std::filesystem::create_directory(kDirectory, error) || error) {
            return std::nullopt;
        }
        std::filesystem::permissions(kDirectory, std::filesystem::perms::owner_all, error);
        held_.own = kDirectory;
    }
    return held_.own;
}

result::Status ShellParticipant::makeChanges() {
    RAWFRAME_TRY_ASSIGN(held_.changesColumn, pane("Changes"));
    return {};
}

void ShellParticipant::runGit(std::string purpose, std::vector<std::string> arguments) {
    held_.gitWaiting.emplace_back(std::move(purpose), std::move(arguments));
    startGit();
}

void ShellParticipant::startGit() {
    if (held_.git.has_value() || held_.gitWaiting.empty()) {
        return;
    }
    // Version control's engine is git, an external tool (SPEC-0051): run as
    // its own program in the game's directory, its output read once it ends.
    const char* const kPath = std::getenv("PATH");
    const auto kProgram = programOnPath("git", kPath != nullptr ? kPath : "");
    if (!kProgram.has_value()) {
        held_.gitWaiting.clear();
        held_.branch.clear();
        say("no git is on the path");
        showChanges();
        return;
    }
    // A batch file's arguments are parsed again by cmd.exe, so a commit's
    // message would run as a command (D453a): only git's own program.
    if (isBatch(*kProgram)) {
        held_.gitWaiting.clear();
        say("git on the path is a batch file; put git's own program on the path");
        return;
    }
    auto [purpose, arguments] = std::move(held_.gitWaiting.front());
    held_.gitWaiting.pop_front();
    // Of the programs a repository's settings name, its file monitor,
    // external diff, and text conversions are never needed to read: they
    // stay off even in a repository the author trusts.
    std::vector<std::string> words = {
        "-c", "core.fsmonitor=false", "-c", "diff.external=", "-C", description_.parent_path().string()};
    words.insert(words.end(), arguments.begin(), arguments.end());
    const std::optional<std::filesystem::path> kOwn = ownDirectory();
    if (!kOwn.has_value()) {
        held_.gitWaiting.clear();
        say("Studio has no directory of its own for git's output");
        return;
    }
    held_.gitOutput = *kOwn / "git.out";
    auto started = process::Child::start({.program = *kProgram, .arguments = words, .output = held_.gitOutput});
    if (!started.has_value()) {
        held_.gitWaiting.clear();
        say("git did not start: " + std::string{started.error().description()});
        return;
    }
    held_.git = std::move(*started);
    held_.gitFor = std::move(purpose);
}

void ShellParticipant::pollGit() {
    if (!held_.git.has_value()) {
        return;
    }
    const std::optional<int> kCode = held_.git->exited();
    if (!kCode.has_value()) {
        return;
    }
    held_.git.reset();
    const std::string kOutput = authoring_session::readFile(held_.gitOutput).value_or("");
    std::error_code error;
    std::filesystem::remove(held_.gitOutput, error);
    const std::string kFor = std::exchange(held_.gitFor, {});
    if (*kCode != 0) {
        // What is waiting stood on what failed; a directory git finds no
        // repository in is not under version control.
        held_.gitWaiting.clear();
        if (kFor == "branch") {
            held_.branch.clear();
            held_.changes.clear();
        } else {
            say("git " + kFor + " failed: " + firstLine(kOutput));
        }
        showChanges();
        return;
    }
    if (kFor == "branch") {
        held_.branch = trimmed(kOutput);
        static_cast<void>(words(held_.branchNode, "branch " + held_.branch, kQuiet, 13));
    } else if (kFor == "prefix") {
        held_.gamePrefix = trimmed(kOutput);
    } else if (kFor == "status") {
        held_.changes = kOutput;
    } else if (kFor == "diff") {
        held_.diff = kOutput;
    } else if (kFor == "commit") {
        ++held_.commits;
        say("committed the game's changes");
        emitter_.log(diagnostics::Severity::Info,
                     kCommitted,
                     "the game's changes committed",
                     {diagnostics::field("branch", held_.branch), diagnostics::field("commits", held_.commits)});
    }
    if (held_.gitWaiting.empty()) {
        showChanges();
    }
    startGit();
}

void ShellParticipant::readChanges() {
    // A repository's own settings may name commands git runs as it reads
    // and commits (filters, hooks): git runs only in one the author has
    // said they trust, as an editor asks before it trusts a workspace.
    if (!held_.gitTrusted) {
        showChanges();
        return;
    }
    runGit("branch", {"rev-parse", "--abbrev-ref", "HEAD"});
    runGit("prefix", {"rev-parse", "--show-prefix"});
    runGit("status", {"status", "--porcelain=v1", "--untracked-files=all", "--", "."});
}

void ShellParticipant::showChanges() {
    clear(held_.changesRows);
    held_.changedPresses.clear();
    commitField_.reset();
    const auto kAdd = [this](std::string_view text, std::uint32_t color) -> std::optional<ui::Node> {
        auto made = row(held_.changesColumn, text, color, kPanel);
        if (!made.has_value()) {
            return std::nullopt;
        }
        held_.changesRows.push_back(*made);
        return *made;
    };
    if (!held_.gitTrusted) {
        kAdd("git runs the commands the game's repository names, its filters and hooks: trust it only if you "
             "trust where the game came from",
             kQuiet);
        auto trust = box(held_.changesColumn, ui::Layout{.height = ui::pixels(26), .shrink = 0}, 0);
        auto pressed = trust.has_value() ? box(*trust, ui::Layout{.padding = {10, 4, 10, 4}}, kRow)
                                         : std::unexpected<result::Error>{trust.error().clone()};
        if (pressed.has_value()) {
            held_.changesRows.push_back(*trust);
            static_cast<void>(pressWords(*pressed, "Trust this repository", kText, 13));
            held_.trustPress = *pressed;
        }
        return;
    }
    if (held_.branch.empty()) {
        kAdd("not under version control: git finds no repository for the game", kQuiet);
        return;
    }
    // The game's documents changed, each pressed for its diff, and a commit
    // of them all with the message typed.
    const std::vector<ChangedFile> kChanged = changedFiles(held_.changes, held_.gamePrefix);
    kAdd("on " + held_.branch + ", " + std::to_string(kChanged.size()) + " changed", kText);
    for (const ChangedFile& kFile : kChanged) {
        auto line = box(held_.changesColumn, ui::Layout{.height = ui::pixels(24), .shrink = 0}, 0);
        auto pressed = line.has_value() ? box(*line, ui::Layout{.grow = 1, .padding = {8, 2, 8, 2}}, kRow)
                                        : std::unexpected<result::Error>{line.error().clone()};
        if (!pressed.has_value()) {
            return;
        }
        held_.changesRows.push_back(*line);
        static_cast<void>(pressWords(*pressed, trimmed(kFile.state) + "  " + kFile.path, kText, 13));
        held_.changedPresses.emplace_back(*pressed, kFile.path);
    }
    if (!kChanged.empty()) {
        if (auto message = fieldRow("commit message", "", held_.changesColumn); message.has_value()) {
            held_.changesRows.push_back(message->first);
            commitField_ = FieldRow{.value = message->second, .role = FieldRow::Role::CommitMessage};
        }
    }
    if (!held_.diffOf.empty()) {
        kAdd("diff of " + held_.diffOf, kQuiet);
        std::size_t shown = 0;
        std::size_t at = 0;
        while (at < held_.diff.size() && shown < kDiffLinesShown) {
            const std::size_t kEnd = std::min(held_.diff.find('\n', at), held_.diff.size());
            const std::string kLine = held_.diff.substr(at, kEnd - at);
            at = kEnd + 1;
            ++shown;
            kAdd(kLine.empty() ? " " : kLine, kLine.starts_with('-') || kLine.starts_with('+') ? kText : kQuiet);
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kChangesShown,
                 "the game's changes as version control sees them",
                 {diagnostics::field("branch", held_.branch),
                  diagnostics::field("changed", kChanged.size()),
                  diagnostics::field("commits", held_.commits)});
}

bool ShellParticipant::pressChanges(ui::Node node) {
    if (!held_.gitTrusted && node == held_.trustPress) {
        held_.gitTrusted = true;
        say("the game's repository trusted");
        readChanges();
        return true;
    }
    if (commitField_.has_value() && node == commitField_->value) {
        beginEdit(*commitField_);
        return true;
    }
    const auto kPress = std::ranges::find(held_.changedPresses, node, &std::pair<ui::Node, std::string>::first);
    if (kPress == held_.changedPresses.end()) {
        return false;
    }
    held_.diffOf = kPress->second;
    held_.diff.clear();
    runGit("diff", {"diff", "--no-ext-diff", "--no-textconv", "HEAD", "--", kPress->second});
    return true;
}

void ShellParticipant::commitChanges(const std::string& message) {
    if (trimmed(message).empty()) {
        say("a commit says what it changes");
        return;
    }
    held_.diffOf.clear();
    runGit("add", {"add", "--all", "--", "."});
    runGit("commit", {"commit", "--quiet", "-m", message, "--", "."});
    readChanges();
}

} // namespace rawframe::studio
