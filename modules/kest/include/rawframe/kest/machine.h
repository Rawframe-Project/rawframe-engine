#pragma once

#include "rawframe/execution/outcome.h"
#include "rawframe/kest/doors.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace rawframe::kest {

/// Who wrote the code a machine runs. Untrusted code waits for Kest's
/// untrusted profile, and starting it is refused until then (ADR-0084).
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

/// A function of the program, found once by name.
struct Entry {
    std::int32_t index = -1;
    /// How many slots its frame needs: the wider of arguments and answer.
    std::uint32_t frameSlots = 0;
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

    /// Calls a function with `frame` holding its arguments and, afterwards,
    /// its answer. Fuel is refilled first, and a cancellation not yet seen
    /// answers `cancelled` without running. A refusal carries the machine's
    /// report as its description.
    [[nodiscard]] execution::TaskOutcome<void> call(Entry entry, std::span<Value> frame);

    /// Asks the machine to stop at its next instruction; the call running, or
    /// the next one, answers `cancelled`. Safe from any thread.
    void cancel(execution::CancelReason reason) noexcept;

    [[nodiscard]] std::uint64_t fuelLeft() const noexcept;
    [[nodiscard]] std::size_t heapUsed() const noexcept;
    /// What the machine has said since last asked.
    [[nodiscard]] std::string takeReport();

private:
    std::unique_ptr<State> state_;
};

} // namespace rawframe::kest
