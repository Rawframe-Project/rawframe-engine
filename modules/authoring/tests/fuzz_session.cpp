// Coverage-guided fuzzing of an authoring session's records (D407), seeded
// with one record of the verbs that carry documents. Whatever a line holds,
// it is read or refused, and its reply is one line naming the id read.

#include "rawframe/authoring/session.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kBytes{reinterpret_cast<const char*>(data), size};
    document::Value id;
    const auto kRecord = authoring::readSessionRecord(kBytes, id);
    const std::string kReply = kRecord.has_value() ? authoring::writeReply(id, document::Value::object())
                                                   : authoring::writeRefusal(id, kRecord.error());
    if (std::ranges::count(kReply, '\n') != 1 || !kReply.ends_with('\n')) {
        std::abort();
    }
    return 0;
}
