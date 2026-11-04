// Coverage-guided fuzzing of a configuration's text, which a host reads
// from its file and a web, Android or iOS client from what it is given.
// What the reader takes stays within its bounds, and every key it holds
// reads as text, as a path, as a number and as a truth without harm; once
// all are read, none is left unread.

#include "rawframe/composition/configuration.h"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kConfiguration = composition::Configuration::parse(kText, "/base");
    if (!kConfiguration.has_value()) {
        return 0;
    }
    if (kConfiguration->size() > composition::kMaximumConfigurationEntries) {
        std::abort();
    }
    std::vector<std::string> keys;
    for (const std::string_view kKey : kConfiguration->unread()) {
        keys.emplace_back(kKey);
    }
    if (keys.size() != kConfiguration->size()) {
        std::abort();
    }
    for (const std::string& kKey : keys) {
        const auto kValue = kConfiguration->text(kKey);
        if (kKey.size() > composition::kMaximumConfigurationKeyBytes || !kValue.has_value() ||
            kValue->size() > composition::kMaximumConfigurationValueBytes || !kConfiguration->path(kKey).has_value()) {
            std::abort();
        }
        static_cast<void>(kConfiguration->unsignedInteger(kKey, 0));
        static_cast<void>(kConfiguration->truth(kKey, false));
    }
    if (!kConfiguration->unread().empty()) {
        std::abort();
    }
    return 0;
}
