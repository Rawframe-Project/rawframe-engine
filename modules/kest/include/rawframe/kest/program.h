#pragma once

// A compiled Kest program (ADR-0084). Kest's own C API stays behind this
// module's headers; nothing above it includes kest.h.

#include "rawframe/result/result.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
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
class Program {
public:
    struct State;

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

    /// For this module's own sources.
    [[nodiscard]] State& state() const noexcept {
        return *state_;
    }

private:
    std::unique_ptr<State> state_;
};

} // namespace rawframe::kest
