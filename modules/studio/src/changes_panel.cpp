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

/// Why git failed: its last `fatal:` or `error:` line, past the warnings
/// it says first (a line end it will change, say), or else its first line.
std::string whyFailed(std::string_view output) {
    std::string why;
    for (std::size_t at = 0; at < output.size();) {
        const std::size_t kEnd = std::min(output.find('\n', at), output.size());
        const std::string kLine = trimmed(output.substr(at, kEnd - at));
        if (kLine.starts_with("fatal:") || kLine.starts_with("error:")) {
            why = kLine;
        }
        at = kEnd + 1;
    }
    return why.empty() ? firstLine(output) : why;
}

/// Whether `directory` or a directory above it holds `.git`, as git looks
/// for a repository, read without running git.
bool inRepository(std::filesystem::path directory) {
    std::error_code error;
    directory = std::filesystem::absolute(directory, error);
    for (; !error && !directory.empty(); directory = directory.parent_path()) {
        if (std::filesystem::exists(directory / ".git", error)) {
            return true;
        }
        if (directory == directory.parent_path()) {
            break;
        }
    }
    return false;
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
    held_.git.ask(std::move(purpose), std::move(arguments));
    startGit();
}

void ShellParticipant::startGit() {
    if (held_.git.running() || !held_.git.waiting()) {
        return;
    }
    // Version control's engine is git, an external tool (SPEC-0051): run as
    // its own program in the game's directory, its output read once it ends.
    const std::optional<std::filesystem::path> kOwn = ownDirectory();
    if (!kOwn.has_value()) {
        held_.git.drop();
        say("Studio has no directory of its own for git's output");
        return;
    }
    const char* const kPath = std::getenv("PATH");
    const auto kRefused = held_.git.start(kPath != nullptr ? kPath : "", description_.parent_path(), *kOwn / "git.out");
    if (!kRefused.has_value()) {
        return;
    }
    say(kRefused->why);
    if (kRefused->kind == VersionControl::Refusal::Kind::NoGit) {
        held_.branch.clear();
        showChanges();
    }
}

void ShellParticipant::pollGit() {
    const std::optional<VersionControl::Ended> kEnded = held_.git.poll();
    if (!kEnded.has_value()) {
        return;
    }
    const std::string& kOutput = kEnded->output;
    const std::string& kFor = kEnded->purpose;
    if (kEnded->code != 0) {
        // What is waiting stood on what failed; a directory git finds no
        // repository in is not under version control.
        held_.git.drop();
        if (kFor == "branch") {
            held_.branch.clear();
            held_.changes.clear();
            held_.gitRefused = whyFailed(kOutput);
        } else {
            say("git " + kFor + " failed: " + whyFailed(kOutput));
        }
        showChanges();
        return;
    }
    if (kFor == "branch") {
        // A repository whose HEAD names no branch is on a commit.
        held_.branch = trimmed(kOutput);
        if (held_.branch.empty()) {
            held_.branch = "a detached HEAD";
        }
        held_.gitRefused.clear();
        static_cast<void>(words(held_.branchNode, "branch " + held_.branch, kQuiet, 13));
    } else if (kFor == "prefix") {
        held_.gamePrefix = trimmed(kOutput);
    } else if (kFor == "status") {
        held_.changes = kOutput;
    } else if (kFor == "diff") {
        held_.diff = kOutput;
    } else if (kFor == "commit") {
        ++held_.commits;
        held_.commitMessage.clear();
        say("committed the game's changes");
        emitter_.log(diagnostics::Severity::Info,
                     kCommitted,
                     "the game's changes committed",
                     {diagnostics::field("branch", held_.branch), diagnostics::field("commits", held_.commits)});
    }
    if (!held_.git.waiting()) {
        showChanges();
    }
    startGit();
}

void ShellParticipant::readChanges() {
    // Git runs only in a repository the author has said they trust
    // (version_control.h); until then the tab asks.
    if (!held_.git.trusted()) {
        showChanges();
        return;
    }
    // The branch's name even before its first commit, which `rev-parse
    // --abbrev-ref HEAD` refuses.
    runGit("branch", {"branch", "--show-current"});
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
    if (!held_.git.trusted()) {
        // A game in no repository has nothing to trust: git is asked
        // nothing, and the author is not asked to trust what is not there.
        if (!inRepository(description_.parent_path())) {
            kAdd("not under version control: the game's folder is in no git repository", kQuiet);
            emitter_.log(diagnostics::Severity::Info,
                         kChangesShown,
                         "the game's changes as version control sees them",
                         {diagnostics::field("branch", std::string{}),
                          diagnostics::field("changed", std::size_t{0}),
                          diagnostics::field("commits", held_.commits),
                          diagnostics::field("repository", false)});
            return;
        }
        kAdd("git runs the commands the game's repository names, its filters and hooks: trust it only if you "
             "trust where the game came from",
             kQuiet);
        auto trust = box(held_.changesColumn, ui::Layout{.shrink = 0, .padding = {8, 8, 4, 4}}, 0);
        auto pressed = trust.has_value() ? textButton(*trust, std::nullopt, "Trust this repository")
                                         : std::unexpected<result::Error>{trust.error().clone()};
        if (pressed.has_value()) {
            held_.changesRows.push_back(*trust);
            held_.trustPress = *pressed;
        }
        return;
    }
    if (held_.branch.empty()) {
        kAdd(held_.gitRefused.empty() ? "not under version control: git finds no repository for the game"
                                      : "git does not read the game's repository: " + held_.gitRefused,
             kQuiet);
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
        if (auto message = fieldRow("commit message", held_.commitMessage, held_.changesColumn); message.has_value()) {
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
                  diagnostics::field("commits", held_.commits),
                  diagnostics::field("repository", true)});
}

bool ShellParticipant::pressChanges(ui::Node node) {
    if (!held_.git.trusted() && node == held_.trustPress) {
        held_.git.trust();
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
    runGit("diff", {"diff", "HEAD", "--", kPress->second});
    return true;
}

void ShellParticipant::commitChanges(const std::string& message) {
    if (trimmed(message).empty()) {
        say("a commit says what it changes");
        return;
    }
    held_.diffOf.clear();
    held_.commitMessage = message;
    runGit("add", {"add", "--all", "--", "."});
    runGit("commit", {"commit", "--quiet", "-m", message, "--", "."});
    readChanges();
}

} // namespace rawframe::studio
