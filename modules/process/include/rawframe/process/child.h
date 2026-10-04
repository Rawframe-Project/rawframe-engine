#pragma once

// A child process a launcher starts (ADR-0084: a launcher may start a local
// dedicated-server process for "host a game"; ADR-0017 keeps the server one
// process of its own). The program is run as it is named, with no shell
// and no search of PATH, its arguments passed as they are. Nothing here
// waits: a child is asked whether it has ended, asked to stop the way a
// Host stops on a stop request, and killed, all without blocking, except
// that a kill reaps what it killed. A child still running when its owner
// goes is killed. Never on the web, which starts no processes.

#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::process {

struct ChildSettings {
    /// The executable file; a relative path is from this process's working
    /// directory.
    std::filesystem::path program;
    /// Its arguments after its own name. Never a secret (ADR-0017): another
    /// process on the machine may read a process's arguments.
    std::vector<std::string> arguments;
    /// Where its standard output and standard error go, the file made
    /// afresh; empty for nowhere.
    std::filesystem::path output;
};

class Child {
public:
    /// Starts the program. Refuses (`unavailable`, StartFailed) what the
    /// system will not run. Where the system cannot tell before the child
    /// runs, a program that cannot be executed ends at once with code 127,
    /// as a shell would report it. On Windows the child inherits this
    /// process's inheritable handles.
    [[nodiscard]] static result::Result<Child> start(const ChildSettings& settings);

    Child(Child&& other) noexcept;
    Child& operator=(Child&& other) noexcept;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    /// Kills a child still running, and reaps it.
    ~Child();

    /// Its exit code once it has ended (one ended by a signal: 128 and the
    /// signal's number), or nothing while it runs. Asks without waiting;
    /// once known, the answer stays.
    [[nodiscard]] std::optional<int> exited() noexcept;

    /// Asks it to stop as a Host stops on a stop request: SIGTERM on POSIX,
    /// Ctrl+Break to its own process group on Windows (where it shares this
    /// process's console). Refuses (`unavailable`, StopFailed) a child that
    /// has ended.
    [[nodiscard]] result::Status requestStop() noexcept;

    /// Ends it at once and reaps it; nothing for one that has ended.
    void kill() noexcept;

    /// The system's number for it, while it is known.
    [[nodiscard]] std::uint64_t id() const noexcept {
        return id_;
    }

private:
    Child() noexcept = default;

    std::uint64_t id_ = 0;
    /// The process's handle on Windows; unused on POSIX.
    void* handle_ = nullptr;
    std::optional<int> code_;
};

} // namespace rawframe::process
