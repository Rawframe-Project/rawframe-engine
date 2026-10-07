#pragma once

#include "rawframe/execution/outcome.h"
#include "rawframe/kest/doors.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::kest {

/// Who wrote the code a machine runs (ADR-0084). Untrusted code is offered
/// only doors marked safe for it and runs under Kest's untrusted profile:
/// only what the verifier proved, with its heap and fuel ceilings.
enum class Trust : std::uint8_t {
    Trusted,
    Untrusted
};

/// Every machine has finite limits (ADR-0014): heap and fuel are required.
/// Stack slots and call depth default to the least the program provably needs
/// for one call in, or refuse the start when Kest cannot prove one.
struct MachineLimits {
    std::uint32_t stackSlots = 0;
    std::uint32_t callDepth = 0;
    std::size_t heapBytes = 0;
    /// Steps one call may take, refilled before every call.
    std::uint64_t fuelPerCall = 0;
};

/// Whether a call starts with a full budget or spends what the last one left.
/// A system called once per archetype spends one budget across its calls.
enum class Fuel : std::uint8_t {
    Refill,
    Continue
};

/// A function of the program, found once by name.
struct Entry {
    std::int32_t index = -1;
    /// How many slots its frame needs: the wider of arguments and answer.
    std::uint32_t frameSlots = 0;
};

/// One argument the engine calls an entry with: a number or truth of a
/// door's slot, or an array the engine lends, whose element Kest checks at
/// each call.
struct Argument {
    Slot slot = Slot::I32;
    bool lent = false;
};

/// One frame of a stopped machine (D460): the function it is in, as it was
/// written, and its arguments by name, each as text: the locals a call has
/// surely written. Others wait for Kest to say which a stop has written.
struct StoppedFrame {
    std::string function;
    std::vector<std::pair<std::string, std::string>> locals;
};

/// One running program with its own stack and heap. Thread-affine: one thread
/// calls it at a time, and only `cancel` may come from another (SPEC-0005).
class Machine {
public:
    struct State;

    [[nodiscard]] static result::Result<std::unique_ptr<Machine>>
    start(std::shared_ptr<const Program> program, const DoorTable& doors, Trust trust, const MachineLimits& limits);

    explicit Machine(std::unique_ptr<State> state) noexcept;
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    ~Machine();

    [[nodiscard]] result::Result<Entry> entry(std::string_view name);
    /// Refuses (`EntryShapeMismatch`) an entry that does not take exactly
    /// `takes`. The frame's width alone does not say this: a lent array's
    /// handle in a number's slot is a number, and whether it fits the slot
    /// depends on the target's pointers (D167).
    [[nodiscard]] result::Status checkArguments(Entry entry, std::span<const Argument> takes);
    /// Refuses (`EntryShapeMismatch`) an entry that does not answer `gives`,
    /// or answers something when `gives` is absent.
    [[nodiscard]] result::Status checkAnswer(Entry entry, std::optional<Slot> gives);

    /// Calls a function with `frame` holding its arguments and, afterwards,
    /// its answer. A cancellation already asked for answers `cancelled`
    /// without running. A refusal carries the machine's report as its
    /// description.
    [[nodiscard]] execution::TaskOutcome<void> call(Entry entry, std::span<Value> frame, Fuel fuel = Fuel::Refill);

    /// Lends the program `length` elements of engine memory as an array of
    /// the program's type `element`, without copying. `elementSize` is what
    /// the engine believes one is and is checked against the program. The
    /// memory must outlive the lend; the program may write through it. Kest
    /// checks at each call that a lent array is of the type the parameter
    /// names.
    [[nodiscard]] result::Result<Value>
    lend(void* data, std::uint32_t length, std::string_view element, std::size_t elementSize);
    /// Ends a lend. Any use of the handle afterwards refuses in the program
    /// rather than reading memory the engine has moved on from.
    void endLend(Value lent) noexcept;

    /// Asks the machine to stop at its next instruction; the call running, or
    /// the next one, answers `cancelled`. Safe from any thread.
    void cancel(execution::CancelReason reason) noexcept;

    /// Debugging (ADR-0066's debugger bridge, D460). Breaks at the start of
    /// each function named as written, with its module (`game.tick`) or
    /// without it, and at no other: the program's breakpoints before are
    /// taken out. Between calls, or while stopped (from the handler). Answers
    /// how many of the names were functions of the program; none while
    /// another machine stands on the program, which would run into them on
    /// whatever thread it runs.
    ///
    /// A breakpoint is written into the program. A breakpoint the machine
    /// stood on is out until the call that met it ends: Kest's public header does not say where the next
    /// instruction starts. The breakpoints go with the machine that wrote
    /// them.
    [[nodiscard]] std::size_t breakAt(std::span<const std::string> functions);
    /// What runs, on the calling thread, each time a call stops at a
    /// breakpoint; the call carries on once it returns. While it runs the
    /// machine is stopped in the middle of the call: `stack` reads it, and
    /// nothing else may be asked of it.
    void whenStopped(std::function<void()> handler);
    /// The frames of a stopped machine, the innermost first; none for one
    /// not stopped.
    [[nodiscard]] std::vector<StoppedFrame> stack() const;
    /// How many times a call stopped.
    [[nodiscard]] std::uint64_t stops() const noexcept;

    [[nodiscard]] std::uint64_t fuelLeft() const noexcept;
    [[nodiscard]] std::size_t heapUsed() const noexcept;
    /// What the machine has said since last asked.
    [[nodiscard]] std::string takeReport();

private:
    std::unique_ptr<State> state_;
};

} // namespace rawframe::kest
