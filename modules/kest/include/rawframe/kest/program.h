#pragma once

// A compiled Kest program (ADR-0084). Kest's own C API stays behind this
// module's headers; nothing above it includes kest.h.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::kest {

/// One source file handed to the compiler: where a build would have found it,
/// and its text. Nothing is read from a disk.
struct SourceFile {
    std::string path;
    std::string text;
};

struct CompileSettings {
    /// Where `std` lives: the path prefix of the library's SourceFiles.
    std::string library;
    /// The most memory reading, checking, and compiling may take. Compiling is
    /// where cost is unbounded, so this is never "as much as there is".
    std::size_t roomBytes = std::size_t{64} * 1024 * 1024;
};

/// Immutable once compiled and shared by every machine started from it, which
/// keep it alive. Compile and start machines from one thread at a time: Kest
/// writes a failed start's report into the build.
/// What one scalar of a laid-out type is. `Other` covers what the engine does
/// not read field by field yet: tags, flags, handles, text.
enum class FieldKind : std::uint8_t {
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    F32,
    F64,
    Bool,
    Other
};

/// One scalar of a type: where it sits and what the program calls it, as a
/// path (`x`, `where.x`, `cells[2]`).
struct Field {
    std::string name;
    std::size_t offset = 0;
    FieldKind kind = FieldKind::Other;
};

/// How the program lays a type out where memory is shared.
struct TypeLayout {
    std::size_t size = 0;
    std::size_t alignment = 0;
    /// Changes whenever the shape does: a field moved, widened, renamed, or a
    /// case or flag inserted. What a save or schema check keeps beside bytes.
    std::uint64_t mark = 0;
    /// Every scalar, in memory order. Empty for a type with a tagged union
    /// inside, which is read by its tag rather than piece by piece.
    std::vector<Field> fields;
};

class Program {
public:
    struct State;

    /// Compiles the program at `path` and what it imports, read from disk:
    /// imports beside it, `std` from `settings.library`. For hosts and tools
    /// that load scripts from files; `compile` is the form that reads nothing.
    [[nodiscard]] static result::Result<std::shared_ptr<const Program>>
    compileFile(const std::string& path, const CompileSettings& settings, std::string* report = nullptr);

    /// Compiles `files`: the first is the program and the rest are what it
    /// imports, the standard library's among them. On failure `report`, if
    /// given, receives the compiler's text.
    [[nodiscard]] static result::Result<std::shared_ptr<const Program>>
    compile(std::span<const SourceFile> files, const CompileSettings& settings, std::string* report = nullptr);

    explicit Program(std::unique_ptr<State> state) noexcept;
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    ~Program();

    /// The doors the program asks for, as `Capability.function` names, in the
    /// order it first reaches them. A door nothing reaches is not asked for.
    [[nodiscard]] std::vector<std::string> doorsRequested() const;
    /// The capabilities it asks for: each door's receiver, once each. What a
    /// program may do is exactly what it is bound, so this is the list a host
    /// reads before deciding.
    [[nodiscard]] std::vector<std::string> capabilitiesRequested() const;

    /// The layout of a type the program declares, by the name a lend uses
    /// (`Position`, or `game.Position` where two modules have one).
    [[nodiscard]] result::Result<TypeLayout> layout(std::string_view type) const;

    /// For this module's own sources.
    [[nodiscard]] State& state() const noexcept {
        return *state_;
    }

private:
    std::unique_ptr<State> state_;
};

} // namespace rawframe::kest
