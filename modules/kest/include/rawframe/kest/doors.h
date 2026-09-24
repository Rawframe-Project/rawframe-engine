#pragma once

// Engine functions a Kest program may call (ADR-0014, ADR-0084). A program
// declares `extern fn World.spawn(...)`; the engine binds a Door of that name.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::kest {

/// One machine word crossing between the engine and a program, laid out
/// exactly as Kest's own value.
union Value {
    std::int64_t integer;
    double real;
    const char* text;
    void* object;
};

/// What one argument or answer of a door is. The subset of Kest's kinds the
/// engine's doors use so far; text crosses in, not out.
enum class Slot : std::uint8_t {
    I32,
    I64,
    U32,
    U64,
    F32,
    F64,
    Bool,
    Text
};

/// One crossing into a door, valid only while the door runs. Arguments are
/// read by their position in the declaration, whatever width each is.
class DoorCall {
public:
    DoorCall(Value* frame, void* machine, std::span<const Slot> takes) noexcept
        : frame_(frame), machine_(machine), takes_(takes) {
    }

    [[nodiscard]] std::int64_t integer(std::size_t argument) const noexcept;
    [[nodiscard]] double real(std::size_t argument) const noexcept;
    [[nodiscard]] bool boolean(std::size_t argument) const noexcept;
    /// Text the program passed; valid only while the door runs.
    [[nodiscard]] std::string_view text(std::size_t argument) const noexcept;

    /// The answer. Written last: it shares slots with the arguments.
    void answerInteger(std::int64_t value) noexcept;
    void answerReal(double value) noexcept;
    void answerBoolean(bool value) noexcept;

    /// The door could not do what it was asked: the call refuses at the
    /// instruction that made it, with these words, and any answer is ignored.
    void fail(std::string_view why) noexcept;
    /// Charges the program's fuel for work the door did, counted in bytes or
    /// elements: one unit for the crossing and one per sixty-four of them, the
    /// rate the machine charges its own instructions.
    void spendFuel(std::uint64_t work) noexcept;

private:
    [[nodiscard]] const Value& at(std::size_t argument) const noexcept;

    Value* frame_;
    void* machine_;
    std::span<const Slot> takes_;
};

using DoorFunction = void (*)(DoorCall& call, void* context) noexcept;

/// An engine function a program may call. Every door says whether untrusted
/// code may reach it: only a door that validates caller, phase, capability,
/// and bounds before any effect is marked safe. The name, slots, and context
/// must outlive every machine started with the table.
struct Door {
    std::string_view name;
    DoorFunction function = nullptr;
    void* context = nullptr;
    std::span<const Slot> takes;
    /// Empty for a door that answers nothing; at most one slot otherwise.
    std::span<const Slot> gives;
    bool safeForUntrusted = false;
};

/// The doors one kind of program may be offered. A machine binds only the
/// doors its program asks for; asking for one not here refuses the start.
class DoorTable {
public:
    [[nodiscard]] result::Status add(const Door& door);

    [[nodiscard]] const Door* find(std::string_view name) const noexcept;
    [[nodiscard]] std::span<const Door> doors() const noexcept {
        return doors_;
    }

private:
    std::vector<Door> doors_;
};

/// Adds the doors `std.math` asks for: `Math.sqrt`, `Math.floor`, and
/// `Math.ceil`. Each is exact on every machine (IEEE-754 rounds a square root
/// correctly; a floor and a ceiling round nothing) and touches nothing, so
/// each is safe for untrusted code.
[[nodiscard]] result::Status addStandardMath(DoorTable& table);

} // namespace rawframe::kest
