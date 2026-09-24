#include "rawframe/kest/program.h"

#include "rawframe/kest/errors.h"
#include "state.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace rawframe::kest {

ReportFile::ReportFile() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    file_ = ::open_memstream(&buffer_, &size_);
#else
    file_ = std::tmpfile();
#endif
}

ReportFile::~ReportFile() {
    if (file_ != nullptr) {
        std::fclose(file_);
    }
    std::free(buffer_);
}

std::string ReportFile::text() {
    if (file_ == nullptr) {
        return {};
    }
    std::string text;
#if defined(__unix__) || defined(__APPLE__)
    std::fflush(file_);
    text.assign(buffer_ != nullptr ? buffer_ : "", size_);
#else
    std::fflush(file_);
    std::rewind(file_);
    char chunk[512];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof chunk, file_)) != 0) {
        text.append(chunk, got);
    }
#endif
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

namespace {

FieldKind fieldKind(std::uint8_t kind) noexcept {
    switch (kind) {
    case KEST_L_I8:
        return FieldKind::I8;
    case KEST_L_I16:
        return FieldKind::I16;
    case KEST_L_I32:
        return FieldKind::I32;
    case KEST_L_I64:
        return FieldKind::I64;
    case KEST_L_U8:
        return FieldKind::U8;
    case KEST_L_U16:
        return FieldKind::U16;
    case KEST_L_U32:
        return FieldKind::U32;
    case KEST_L_U64:
        return FieldKind::U64;
    case KEST_L_F32:
        return FieldKind::F32;
    case KEST_L_F64:
        return FieldKind::F64;
    case KEST_L_BOOL:
        return FieldKind::Bool;
    default:
        return FieldKind::Other;
    }
}

result::Status checkAbi() {
    // The header and the library must be one version of Kest: every struct
    // below is a promise only the matching library keeps.
    if (kest_abi_version() != KEST_ABI_VERSION) {
        return result::fail(result::ErrorClass::Internal,
                            kKestDomain,
                            code(KestError::AbiMismatch),
                            "the linked Kest library is not the version its header describes");
    }
    return {};
}

result::Result<std::shared_ptr<const Program>> finish(KestBuild* build, ReportFile& errors, std::string* report) {
    if (build == nullptr) {
        if (report != nullptr) {
            *report = errors.text();
        }
        return result::fail(result::ErrorClass::InvalidArgument,
                            kKestDomain,
                            code(KestError::DoesNotCompile),
                            "the Kest program does not compile");
    }
    auto state = std::make_unique<Program::State>();
    state->build = build;
    return std::make_shared<const Program>(std::move(state));
}

} // namespace

Program::Program(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Program::~Program() = default;

result::Result<std::shared_ptr<const Program>>
Program::compile(std::span<const SourceFile> files, const CompileSettings& settings, std::string* report) {
    RAWFRAME_TRY(checkAbi());
    if (files.empty() || settings.roomBytes == 0) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kKestDomain,
                            code(KestError::DoesNotCompile),
                            "a program needs at least one file and a finite room to compile in");
    }
    std::vector<KestFile> handed;
    handed.reserve(files.size());
    for (const SourceFile& file : files) {
        handed.push_back(KestFile{.path = file.path.c_str(), .text = file.text.c_str(), .length = file.text.size()});
    }
    ReportFile errors;
    KestBuild* const kBuild = kest_build_from(handed.data(),
                                              static_cast<std::uint32_t>(handed.size()),
                                              settings.library.empty() ? nullptr : settings.library.c_str(),
                                              errors.file(),
                                              KEST_FORM_TEXT,
                                              settings.roomBytes);
    return finish(kBuild, errors, report);
}

result::Result<std::shared_ptr<const Program>>
Program::compileFile(const std::string& path, const CompileSettings& settings, std::string* report) {
    RAWFRAME_TRY(checkAbi());
    if (path.empty() || settings.roomBytes == 0) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kKestDomain,
                            code(KestError::DoesNotCompile),
                            "a program needs a path and a finite room to compile in");
    }
    ReportFile errors;
    KestBuild* const kBuild = kest_build(path.c_str(),
                                         settings.library.empty() ? nullptr : settings.library.c_str(),
                                         errors.file(),
                                         KEST_FORM_TEXT,
                                         settings.roomBytes);
    return finish(kBuild, errors, report);
}

std::vector<std::string> Program::doorsRequested() const {
    std::vector<std::string> names;
    for (std::uint32_t at = 0; const char* const kName = kest_build_extern(state_->build, at); ++at) {
        names.emplace_back(kName);
    }
    return names;
}

std::vector<std::string> Program::capabilitiesRequested() const {
    std::vector<std::string> names;
    for (std::uint32_t at = 0; const char* const kName = kest_build_capability(state_->build, at); ++at) {
        names.emplace_back(kName);
    }
    return names;
}

result::Result<TypeLayout> Program::layout(std::string_view type) const {
    const std::string kName{type};
    const KestLayout* found = nullptr;
    if (kest_build_layout(state_->build, kName.c_str(), &found) != 1 || found == nullptr) {
        return result::fail(result::ErrorClass::NotFound,
                            kKestDomain,
                            code(KestError::UnknownType),
                            "the program declares no one type of this name");
    }
    TypeLayout layout{.size = found->size, .alignment = found->align, .mark = kest_layout_mark(found), .fields = {}};
    if (!found->tagged) {
        for (std::uint16_t index = 0; index < found->count; ++index) {
            const KestPiece& piece = found->pieces[index];
            layout.fields.push_back(Field{.name = piece.name != nullptr ? piece.name : "",
                                          .offset = piece.offset,
                                          .kind = fieldKind(piece.kind)});
        }
    }
    return layout;
}

} // namespace rawframe::kest
