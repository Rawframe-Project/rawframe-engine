#include "files.h"

#include "rawframe/base/files.h"
#include "rawframe/install/errors.h"

#include <fstream>
#include <string>

namespace rawframe::install {

namespace {

std::unexpected<result::Error> unwritten(std::string_view why, const std::filesystem::path& path) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kInstallDomain, code(InstallError::WriteFailed), why)
            .error()
            .withContext("path", path.generic_string())};
}

} // namespace

result::Result<std::vector<std::byte>>
readFile(const std::filesystem::path& path, std::uint64_t ceiling, std::string_view absent) {
    std::error_code error;
    const auto kStatus = std::filesystem::symlink_status(path, error);
    const std::uint64_t kSize =
        error || !std::filesystem::is_regular_file(kStatus) ? ceiling + 1 : std::filesystem::file_size(path, error);
    if (error || kSize > ceiling) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::Unavailable, kInstallDomain, code(InstallError::FetchFailed), absent)
                .error()
                .withContext("path", path.generic_string())};
    }
    std::vector<std::byte> bytes(kSize);
    std::ifstream file{path, std::ios::binary};
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::ifstream::traits_type::eof()) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::Unavailable,
                                                           kInstallDomain,
                                                           code(InstallError::FetchFailed),
                                                           "a file cannot be read whole")
                                                  .error()
                                                  .withContext("path", path.generic_string())};
    }
    return bytes;
}

result::Status
publishFile(const std::filesystem::path& staged, const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::error_code error;
    std::filesystem::create_directories(staged.parent_path(), error);
    std::filesystem::create_directories(path.parent_path(), error);
    if (!base::writeWholeFile(staged, bytes)) {
        std::filesystem::remove(staged, error);
        return unwritten("a file cannot be written", staged);
    }
    std::filesystem::rename(staged, path, error);
    if (error) {
        std::filesystem::remove(staged, error);
        return unwritten("a written file cannot be put in place", path);
    }
    base::flushDirectory(path.parent_path());
    return {};
}

} // namespace rawframe::install
