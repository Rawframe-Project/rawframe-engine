// Coverage-guided fuzzing of the records content reads from a Build (D242):
// a manifest, a sidecar, and a composition record. The composition reader
// is exact; a manifest may list its entries in any order, so what it reads
// must write to bytes that read and write to themselves.

#include "rawframe/content/composition_record.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/sidecar.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    static_cast<void>(content::readSidecar(kText));

    if (const auto kRecord = content::readComposition(kText); kRecord.has_value()) {
        const auto kWritten = content::writeComposition(*kRecord);
        if (!kWritten.has_value() || *kWritten != kText) {
            std::abort();
        }
    }

    if (const auto kEntries = content::readManifest(kText); kEntries.has_value()) {
        const std::string kOnce = content::writeManifest(*kEntries);
        const auto kAgain = content::readManifest(kOnce);
        if (!kAgain.has_value() || content::writeManifest(*kAgain) != kOnce) {
            std::abort();
        }
    }
    return 0;
}
