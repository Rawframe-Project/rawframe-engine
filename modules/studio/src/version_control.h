#pragma once

// Git as Studio's Changes tab runs it: one command at a time in the game's
// directory, in order, its output read once it ends. A repository's own
// settings may name commands git runs as it reads and commits (filters,
// hooks), so git runs only in a repository the author has said they trust,
// as an editor asks before it trusts a workspace. Even there, the programs
// a repository names that reading never needs stay off: its file monitor,
// its external diff, and its text conversions. Nothing here waits.

#include "rawframe/process/child.h"

#include <cstddef>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

class VersionControl {
public:
    /// Why what waited did not start; what waited is dropped.
    struct Refusal {
        enum class Kind {
            /// No git on the path.
            NoGit,
            /// Git on the path is a batch file, whose arguments cmd.exe
            /// parses again, so a commit's message would run as a command.
            Batch,
            /// The system would not start it.
            Failed,
        };
        Kind kind = Kind::Failed;
        std::string why;
    };

    /// A command that ended: what it was asked for, its exit code, and what
    /// it wrote.
    struct Ended {
        std::string purpose;
        int code = 0;
        std::string output;
    };

    /// Whether the author trusts the repository.
    [[nodiscard]] bool trusted() const noexcept {
        return trusted_;
    }
    /// The author trusts the repository: git may run in it from now on.
    void trust() noexcept {
        trusted_ = true;
    }

    /// Asks git `arguments` for `purpose`, after what waits. Nothing is
    /// kept while the repository is not trusted.
    void ask(std::string purpose, std::vector<std::string> arguments);

    /// Starts what waits, where nothing runs, with git found on `path` (a
    /// PATH value), in `directory`, its output written to `output`.
    [[nodiscard]] std::optional<Refusal>
    start(std::string_view path, const std::filesystem::path& directory, const std::filesystem::path& output);

    /// What ran, once it has ended.
    [[nodiscard]] std::optional<Ended> poll();

    /// Drops what waits.
    void drop() noexcept {
        waiting_.clear();
    }
    /// Stops what runs and drops what waits.
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept {
        return child_.has_value();
    }
    [[nodiscard]] bool waiting() const noexcept {
        return !waiting_.empty();
    }
    /// How many git processes have started.
    [[nodiscard]] std::size_t started() const noexcept {
        return started_;
    }

    /// Git's arguments for one command in `directory`: the file monitor and
    /// external diff off for every command, and for a diff, external diffs
    /// and text conversions off too.
    [[nodiscard]] static std::vector<std::string> words(const std::filesystem::path& directory,
                                                        const std::vector<std::string>& arguments);

private:
    bool trusted_ = false;
    std::deque<std::pair<std::string, std::vector<std::string>>> waiting_;
    std::optional<process::Child> child_;
    std::string runningFor_;
    std::filesystem::path output_;
    std::size_t started_ = 0;
};

} // namespace rawframe::studio
