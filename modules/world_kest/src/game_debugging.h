#pragma once

// A game's Kest debugged (D460): the functions an author breaks at, written
// into the program the game's systems run and again into each it reloads
// to, and a stop served by whoever debugs it until told to carry on.

#include "rawframe/execution/time.h"
#include "rawframe/kest/machine.h"
#include "rawframe/world_runtime/debugging.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rawframe::world_kest {

class GameDebugging final : public world_runtime::Debugging {
public:
    /// The machine the game's systems now run on, after a start or a
    /// reload: the breakpoints asked for are written into its program.
    void attach(kest::Machine& machine) noexcept;
    /// Whom each stop is told to, with how long it stood by `clock`: the
    /// World, which owes no ticks for it (D492).
    void tellStood(const execution::MonotonicSource& clock,
                   std::function<void(execution::MonotonicDuration)> stood) noexcept;

    [[nodiscard]] std::size_t breakAt(std::vector<std::string> functions) noexcept override;
    void whileStopped(std::function<bool()> serve) noexcept override;
    [[nodiscard]] bool stopped() const noexcept override;
    [[nodiscard]] std::uint64_t stops() const noexcept override;
    [[nodiscard]] std::vector<world_runtime::DebugFrame> stack() const override;

private:
    void stop() noexcept;

    kest::Machine* machine_ = nullptr;
    std::vector<std::string> functions_;
    std::function<bool()> serve_;
    const execution::MonotonicSource* clock_ = nullptr;
    std::function<void(execution::MonotonicDuration)> stood_;
    bool stopped_ = false;
    std::uint64_t stops_ = 0;
};

} // namespace rawframe::world_kest
