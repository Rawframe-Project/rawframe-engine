#pragma once

// What this module keeps of Kest's C API. Private: only src/ includes kest.h.

#include "rawframe/kest/doors.h"
#include "rawframe/kest/program.h"

// Kest's header declares C functions without saying so to C++.
extern "C" {
#include "kest.h"
}

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rawframe::kest {

struct Program::State {
    KestBuild* build = nullptr;

    /// A byte of the build written over with the breakpoint instruction,
    /// and what was there. The build's, so every machine of the
    /// program can take one out to carry on and put it back.
    struct Written {
        std::int32_t entry = -1;
        std::uint32_t at = 0;
        std::uint8_t was = 0;
        /// Taken out while a machine carries on from it.
        bool out = false;
    };
    std::vector<Written> written;
    /// How many machines stand on the program: breakpoints are written only
    /// into one that a single machine does, so no other, on whatever thread,
    /// runs into them.
    std::atomic<std::uint32_t> machines{0};

    /// Puts a written byte back, or writes the breakpoint again, through any
    /// machine of the program.
    void put(KestRuntime* runtime, Written& one, bool breaking) const {
        write(runtime, one, breaking);
        one.out = !breaking;
    }

    /// The byte alone, whether it is out unchanged: a walk of a body reads it
    /// as compiled, its breakpoints put back for the walk.
    void write(KestRuntime* runtime, const Written& one, bool breaking) const {
        std::uint32_t count = 0;
        std::uint8_t* const kCode = kest_code_of(runtime, one.entry, &count);
        if (kCode != nullptr && one.at < count) {
            kCode[one.at] = breaking ? kest_break_byte() : one.was;
        }
    }

    ~State() {
        // Every machine holds the Program alive, so none stands on the build
        // by now and freeing cannot be refused.
        static_cast<void>(kest_build_free(build));
    }
};

/// How a value crosses a door as bytes, worked out once when the door is
/// bound: at every crossing a piece is one copy, not a walk of its kind.
struct Crossing {
    /// How one slot becomes the bytes of its piece and back.
    enum class Copy : std::uint8_t {
        Real32,
        Real64,
        Unsigned8,
        Unsigned16,
        Unsigned32,
        Signed8,
        Signed16,
        Signed32,
        Whole64
    };
    struct Piece {
        std::uint32_t offset = 0;
        Copy copy = Copy::Whole64;
    };

    /// False for an argument or answer that is not a value.
    bool value = false;
    std::uint32_t size = 0;
    /// Pieces in slot order, one slot each.
    std::vector<Piece> pieces;
    /// Whether bytes lie between or after the pieces, zeroed on the way out.
    bool gaps = false;
};

struct DoorCall::Shape {
    /// Per argument: its first slot in the frame, and how it crosses.
    std::vector<std::uint32_t> offsets;
    std::vector<Crossing> takes;
    Crossing gives;
};

/// Whether a value of this layout can cross a door as bytes: numbers, truths,
/// tags, and flags, one slot per piece, no tagged union.
[[nodiscard]] bool crossesByValue(const KestLayout* layout) noexcept;

/// How a value of a layout `crossesByValue` crosses.
[[nodiscard]] Crossing crossingOf(const KestLayout* layout);

/// A FILE Kest writes a report into, read back as text. In memory where the
/// platform offers it; a temporary file elsewhere.
class ReportFile {
public:
    ReportFile() noexcept;
    ReportFile(const ReportFile&) = delete;
    ReportFile& operator=(const ReportFile&) = delete;
    ~ReportFile();

    /// Null when no file could be made: Kest then writes nowhere.
    [[nodiscard]] std::FILE* file() noexcept {
        return file_;
    }
    /// Everything written so far, trailing newlines removed.
    [[nodiscard]] std::string text();

private:
    std::FILE* file_ = nullptr;
    char* buffer_ = nullptr;
    // Written by open_memstream, which Windows has not.
    [[maybe_unused]] std::size_t size_ = 0;
};

} // namespace rawframe::kest
