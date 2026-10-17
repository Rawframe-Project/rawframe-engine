#include "rawframe/cook/sequence.h"

#include "rawframe/cook/errors.h"
#include "rawframe/sequence/sequence.h"

namespace rawframe::cook {

namespace {

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kCookDomain,
                            code(CookError::BadSidecar),
                            "a sequence takes no settings");
    }
    return std::string{};
}

result::Result<Artifact> cookSequence(std::span<const std::byte> source, std::string_view, Reads&) {
    RAWFRAME_TRY(sequence::parseSequence({reinterpret_cast<const char*>(source.data()), source.size()}));
    return Artifact{.type = content::ResourceTypeId{sequence::kSequenceType},
                    .representation = *content::RepresentationId::parse(sequence::kSequenceRepresentation),
                    .bytes = {source.begin(), source.end()}};
}

} // namespace

Importer sequenceImporter() noexcept {
    return Importer{.identity = "rawframe.sequence", .normalize = &normalize, .cook = &cookSequence};
}

} // namespace rawframe::cook
