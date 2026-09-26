// Coverage-guided fuzzing of an authoring journal read from disk (D242),
// seeded with one delta of every kind. The reader is exact: a journal it
// accepts writes back to the very same bytes.

#include "rawframe/authoring/delta.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kBytes{reinterpret_cast<const char*>(data), size};
    const auto kJournal = authoring::readJournal(kBytes);
    if (!kJournal.has_value()) {
        return 0;
    }
    const auto kWritten = authoring::writeJournal(*kJournal);
    if (!kWritten.has_value() || *kWritten != kBytes) {
        std::abort();
    }
    return 0;
}
