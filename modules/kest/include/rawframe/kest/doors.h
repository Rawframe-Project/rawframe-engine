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

/// What one argument or answer of a door is. `Value` is a struct the program
/// declares, of numbers and truths only, crossing by value; text crosses in,
/// not out.
enum class Slot : std::uint8_t {
    I32,
    I64,
    U32,
    U64,
    F32,
    F64,
    Bool,
    Text,
    Value
};

/// One argument or answer: its slot, and for `Slot::Value` the program's
/// name for the type (`Entity`, or `rawframe.world.Entity`).
struct Parameter {
    Slot slot = Slot::I64;
    std::string_view type;

    // Implicit, so a scalar parameter is written as its Slot.
    constexpr Parameter(Slot kind) noexcept : slot(kind) {
    }
    constexpr Parameter(Slot kind, std::string_view name) noexcept : slot(kind), type(name) {
    }
};

/// One crossing into a door, valid only while the door runs. Arguments are
/// read by their position in the declaration, whatever width each is.
class DoorCall {
public:
    /// How a bound door's arguments sit in the frame. This module's own.
    struct Shape;

    DoorCall(Value* frame, void* machine, const Shape& shape) noexcept
        : frame_(frame), machine_(machine), shape_(&shape) {
    }

    [[nodiscard]] std::int64_t integer(std::size_t argument) const noexcept;
    [[nodiscard]] double real(std::size_t argument) const noexcept;
    [[nodiscard]] bool boolean(std::size_t argument) const noexcept;
    /// Text the program passed; valid only while the door runs.
    [[nodiscard]] std::string_view text(std::size_t argument) const noexcept;
    /// A `Slot::Value` argument, written into `into` exactly as the program
    /// lays the type out where memory is shared. False, writing nothing, when
    /// `into` is not the type's size.
    [[nodiscard]] bool value(std::size_t argument, std::span<std::byte> into) const noexcept;

    /// The answer. Written last: it shares slots with the arguments.
    void answerInteger(std::int64_t value) noexcept;
    void answerReal(double value) noexcept;
    void answerBoolean(bool value) noexcept;
    /// A `Slot::Value` answer from bytes laid out as the program lays the type
    /// out. False, writing nothing, when `from` is not the type's size.
    [[nodiscard]] bool answerValue(std::span<const std::byte> from) noexcept;

    /// The door could not do what it was asked: the call refuses at the
    /// instruction that made it, with these words, and any answer is ignored.
    void fail(std::string_view why) noexcept;
    /// Charges the program's fuel for work the door did, counted in bytes or
    /// elements: one unit for the crossing and one per sixty-four of them, the
    /// rate the machine charges its own instructions.
    void spendFuel(std::uint64_t work) noexcept;

private:
    [[nodiscard]] Value* at(std::size_t argument) const noexcept;

    Value* frame_;
    void* machine_;
    const Shape* shape_;
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
    std::span<const Parameter> takes;
    /// Empty for a door that answers nothing; at most one otherwise.
    std::span<const Parameter> gives;
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
