#pragma once

// A game's scripts debugged (ADR-0066's debugger bridge, D460): breakpoints
// at functions of the running program, and a stopped game's frames, from
// whoever runs the program (a game, through its Kest machine), so a reader
// that does not, the tooling endpoint, can serve a debugger. A stop is the
// middle of a tick: the game's thread waits in it, serving what a debugger
// asks, until told to carry on. Asked on the Host thread.

#include "rawframe/composition/participant.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::world_runtime {

/// One frame of a stopped game: the function it is in, as written with its
/// module, and its named locals, each as text.
struct DebugFrame {
    std::string function;
    std::vector<std::pair<std::string, std::string>> locals;
};

class Debugging {
public:
    Debugging() = default;
    Debugging(const Debugging&) = delete;
    Debugging& operator=(const Debugging&) = delete;
    virtual ~Debugging() = default;

    /// Breaks at the start of each function named, from now on and in every
    /// program the game reloads to, and at no other; between ticks or while
    /// stopped. Answers how many of the names are functions of the running
    /// program.
    [[nodiscard]] virtual std::size_t breakAt(std::vector<std::string> functions) noexcept = 0;
    /// What runs while the game is stopped, again and again, a few
    /// milliseconds apart, until it answers true: carry on. None carries on
    /// at once.
    virtual void whileStopped(std::function<bool()> serve) noexcept = 0;
    [[nodiscard]] virtual bool stopped() const noexcept = 0;
    /// How many times the game stopped.
    [[nodiscard]] virtual std::uint64_t stops() const noexcept = 0;
    /// While stopped, the frames, the innermost first; none otherwise.
    [[nodiscard]] virtual std::vector<DebugFrame> stack() const = 0;
};

inline constexpr composition::Capability<Debugging> kDebugging{"rawframe.world_runtime.debugging"};

} // namespace rawframe::world_runtime
