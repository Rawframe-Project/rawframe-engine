// Coverage-guided fuzzing of the text a game's Build carries for its
// players (D243): string tables, translations, the messages they hold, and
// locale tags. The tables are read in their canonical bytes only, so what
// reads writes back to the very same text.

#include "rawframe/localization/locale.h"
#include "rawframe/localization/message.h"
#include "rawframe/localization/table.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

namespace {

template <typename Read, typename Write> void exact(std::string_view text, Read read, Write write) {
    const auto kRead = read(text, {});
    if (!kRead.has_value()) {
        return;
    }
    const auto kWritten = write(*kRead, {});
    if (!kWritten.has_value() || *kWritten != text) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    exact(kText, localization::readStrings, localization::writeStrings);
    exact(kText, localization::readTranslations, localization::writeTranslations);
    static_cast<void>(localization::parseMessage(kText));
    static_cast<void>(localization::parseLocale(kText));
    return 0;
}
