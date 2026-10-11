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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::world_runtime {

/// A line of a game's Kest source (D630): its file, the path the build
/// read it by or any end of that path after a separator, and its line,
/// counted from one.
struct DebugLine {
    std::string file;
    std::uint32_t line = 0;
};

/// How a stopped game steps (D632): to the next line wherever it is
/// (`Into`), the next in this function or one it returns to (`Over`), or
/// the line it returns to (`Out`).
enum class DebugStep : std::uint8_t {
    Over,
    Into,
    Out
};

/// Where `breakAt` broke: how many of the functions named are the
/// program's, and for each line asked, in order, whether code comes from it.
struct DebugBroken {
    std::size_t functions = 0;
    std::vector<bool> lines;
};

/// One frame of a stopped game: the function it is in, as written with its
/// module; the line it stands at (D630), none where Kest cannot say; and
/// its named locals, each as text.
struct DebugFrame {
    std::string function;
    std::optional<DebugLine> at;
    std::vector<std::pair<std::string, std::string>> locals;
};

class Debugging {
public:
    Debugging() = default;
    Debugging(const Debugging&) = delete;
    Debugging& operator=(const Debugging&) = delete;
    virtual ~Debugging() = default;

    /// Breaks at the start of each function named and at each line given
    /// (D630), from now on and in every program the game reloads to, and at
    /// no other; between ticks or while stopped. A breakpoint stopped at is
    /// stepped past, so it stops the game again as it comes round.
    [[nodiscard]] virtual DebugBroken breakAt(std::vector<std::string> functions,
                                              std::vector<DebugLine> lines = {}) noexcept = 0;
    /// What runs while the game is stopped, again and again, a few
    /// milliseconds apart, until it answers true: carry on. None carries on
    /// at once.
    virtual void whileStopped(std::function<bool()> serve) noexcept = 0;
    /// While stopped, from what serves the stop (D632): once it answers
    /// carry on, the game runs only to the next line as `how` says and
    /// stops again; a breakpoint met on the way stops it there instead, and
    /// a function that ends on the way ends the step. False while not
    /// stopped.
    virtual bool step(DebugStep how) noexcept = 0;
    [[nodiscard]] virtual bool stopped() const noexcept = 0;
    /// How many times the game stopped.
    [[nodiscard]] virtual std::uint64_t stops() const noexcept = 0;
    /// While stopped, the frames, the innermost first; none otherwise.
    [[nodiscard]] virtual std::vector<DebugFrame> stack() const = 0;
};

inline constexpr composition::Capability<Debugging> kDebugging{"rawframe.world_runtime.debugging"};

} // namespace rawframe::world_runtime
