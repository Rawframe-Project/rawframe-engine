#include "rawframe/cook/kest.h"

#include "rawframe/cook/errors.h"
#include "rawframe/kest_library/library.h"

namespace rawframe::cook {

namespace {

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadSidecar), why).error()};
}

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return refuse("a Kest project takes no settings");
    }
    return std::string{};
}

result::Result<Artifact> cookProject(std::span<const std::byte>, std::string_view, Reads& reads) {
    RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kNames, reads.files(".", ".kest"));
    std::vector<kest::SourceFile> files;
    for (const std::string& name : kNames) {
        RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kBytes, reads.file(name));
        files.push_back(kest::SourceFile{
            .path = name, .text = std::string{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()}});
    }
    RAWFRAME_TRY_ASSIGN(std::string written, kest_library::writeGameSources(files));
    const auto kBytes = std::as_bytes(std::span{written.data(), written.size()});
    return Artifact{.type = content::ResourceTypeId{kest_library::kGameSourcesType},
                    .representation = *content::RepresentationId::parse(kest_library::kGameSourcesRepresentation),
                    .bytes = {kBytes.begin(), kBytes.end()}};
}

} // namespace

Importer kestImporter() noexcept {
    return Importer{.identity = "rawframe.kest", .normalize = &normalize, .cook = &cookProject};
}

} // namespace rawframe::cook
