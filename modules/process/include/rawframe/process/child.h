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
    /// Whether it stops when this process ends, however this process ends
    /// (D646): it is told this process's identity in `RAWFRAME_PARENT`, and
    /// a Host so told asks the system to tell it when its parent goes
    /// (Linux a signal, macOS a process source, Windows a wait on the
    /// parent), which it takes as any stop, in order.
    bool stopsWithThis = false;
};

/// The environment variable a child that stops with its parent is told
/// its parent's identity in.
inline constexpr const char* kParentVariable = "RAWFRAME_PARENT";

/// The environment variable a child on Windows is told the handle of its
/// stop event in (D944): an event it inherits, set when it is asked to
/// stop. A Host waits on it, as a POSIX child takes SIGTERM; a child has a
/// console of its own with no window, so no console event reaches it from
/// this process.
inline constexpr const char* kStopVariable = "RAWFRAME_STOP";

class Child {
public:
    /// Starts the program. Refuses (`unavailable`, StartFailed) what the
    /// system will not run. Where the system cannot tell before the child
    /// runs, a program that cannot be executed ends at once with code 127,
    /// as a shell would report it. On Windows the child inherits only its
    /// input, its output, and its stop event, and runs with a console of its
    /// own that shows no window (D944), whether this process has a console
    /// or is a windowed program.
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
    /// its stop event set on Windows (`kStopVariable`). Refuses
    /// (`unavailable`, StopFailed) a child that has ended.
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
    /// Its stop event on Windows; unused on POSIX.
    void* stop_ = nullptr;
    std::optional<int> code_;
};

} // namespace rawframe::process
