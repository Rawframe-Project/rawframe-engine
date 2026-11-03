// Coverage-guided fuzzing of a sequence document, which a mod's Build can
// hold and the Runtime reads and plays. What the reader takes, the writer
// writes in a form it reads back to the same sequence, and that form writes
// as itself; playing it passes only its own keys.

#include "rawframe/sequence/sequence.h"

#include <cstdlib>
#include <string>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kSequence = sequence::parseSequence(kText);
    if (!kSequence.has_value()) {
        return 0;
    }
    const std::string kWritten = sequence::writeSequence(*kSequence);
    const auto kAgain = sequence::parseSequence(kWritten);
    if (!kAgain.has_value() || *kAgain != *kSequence || sequence::writeSequence(*kAgain) != kWritten) {
        std::abort();
    }
    for (const double kSeconds : {kSequence->duration / 3, kSequence->duration * 3}) {
        const sequence::Advance kAdvance = sequence::advance(*kSequence, kSequence->duration / 2, kSeconds);
        for (const std::size_t kKey : kAdvance.keys) {
            if (kKey >= kSequence->keys.size()) {
                std::abort();
            }
        }
    }
    return 0;
}
