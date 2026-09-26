#pragma once

// What this module keeps of Kest's C API. Private: only src/ includes kest.h.

#include "rawframe/kest/doors.h"
#include "rawframe/kest/program.h"

// Kest's header declares C functions without saying so to C++.
extern "C" {
#include "kest.h"
}

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rawframe::kest {

struct Program::State {
    KestBuild* build = nullptr;

    ~State() {
        // Every machine holds the Program alive, so none stands on the build
        // by now and freeing cannot be refused.
        static_cast<void>(kest_build_free(build));
    }
};

struct DoorCall::Shape {
    /// Per argument: its slot kind, its first slot in the frame, and for a
    /// value the program's layout of it (the build's, which outlives this).
    std::vector<Slot> slots;
    std::vector<std::uint32_t> offsets;
    std::vector<const KestLayout*> layouts;
    const KestLayout* gives = nullptr;
};

/// Whether a value of this layout can cross a door as bytes: numbers, truths,
/// tags, and flags, one slot per piece, no tagged union.
[[nodiscard]] bool crossesByValue(const KestLayout* layout) noexcept;

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
