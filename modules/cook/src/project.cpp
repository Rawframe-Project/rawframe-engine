#include "project.h"

#include "rawframe/content/sidecar.h"
#include "rawframe/cook/errors.h"
#include "rawframe/kest_library/library.h"

#include <string>

namespace rawframe::cook {

namespace {

std::unexpected<result::Error> refuse(std::string_view why, std::string_view name) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadReference), why)
            .error()
            .withContext("name", name)};
}

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace

result::Result<KestProject> projectBeside(Reads& reads, std::string_view program) {
    auto sidecarBytes = reads.file(std::string{"kest.project"} + std::string{content::kSidecarSuffix});
    if (!sidecarBytes.has_value()) {
        return refuse("a program's kest.project is beside the description, with a sidecar", program);
    }
    RAWFRAME_TRY_ASSIGN(const content::Sidecar kSidecar, content::readSidecar(textOf(*sidecarBytes)));
    if (kSidecar.importer != "rawframe.kest") {
        return refuse("the project beside the description is cooked by rawframe.kest", program);
    }
    KestProject project{.sources = kSidecar.id.value};
    RAWFRAME_TRY_ASSIGN(const std::vector<std::string> kNames, reads.files(".", ".kest"));
    for (const std::string& name : kNames) {
        RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kBytes, reads.file(name));
        project.files.push_back(kest::SourceFile{.path = name, .text = std::string{textOf(kBytes)}});
    }
    return project;
}

result::Status compiles(const KestProject& project, std::string_view program) {
    if (!kest_library::plainGamePath(program)) {
        return refuse("a program is named by a plain path under the description's directory", program);
    }
    std::string report;
    if (!kest_library::compile(program, project.files, {}, &report).has_value()) {
        return std::unexpected<result::Error>{
            refuse("a program does not compile from its sources", program).error().withContext("report", report)};
    }
    return {};
}

} // namespace rawframe::cook
