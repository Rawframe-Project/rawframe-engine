#include "files.h"

#include "rawframe/install/errors.h"

#include <cstdio>
#include <fstream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h>
#endif

namespace rawframe::install {

namespace {

std::unexpected<result::Error> unwritten(std::string_view why, const std::filesystem::path& path) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kInstallDomain, code(InstallError::WriteFailed), why)
            .error()
            .withContext("path", path.generic_string())};
}

/// Writes and flushes a whole file.
bool writeWhole(const std::filesystem::path& path, std::span<const std::byte> bytes) {
#if defined(__unix__) || defined(__APPLE__)
    const int kFile = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (kFile < 0) {
        return false;
    }
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t kWrote = ::write(kFile, bytes.data() + written, bytes.size() - written);
        if (kWrote <= 0) {
            ::close(kFile);
            return false;
        }
        written += static_cast<std::size_t>(kWrote);
    }
    const bool kFlushed = ::fsync(kFile) == 0;
    return ::close(kFile) == 0 && kFlushed;
#else
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
#if defined(_WIN32)
    // Past the C library's buffer and the system's cache, to the disk.
    const bool kFlushed = std::fflush(file) == 0 && ::_commit(::_fileno(file)) == 0;
#else
    const bool kFlushed = std::fflush(file) == 0;
#endif
    return std::fclose(file) == 0 && kWritten && kFlushed;
#endif
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
    if (!writeWhole(staged, bytes)) {
        std::filesystem::remove(staged, error);
        return unwritten("a file cannot be written", staged);
    }
    std::filesystem::rename(staged, path, error);
    if (error) {
        std::filesystem::remove(staged, error);
        return unwritten("a written file cannot be put in place", path);
    }
    flushDirectory(path.parent_path());
    return {};
}

void flushDirectory([[maybe_unused]] const std::filesystem::path& directory) {
#if defined(__unix__) || defined(__APPLE__)
    const int kDirectory = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (kDirectory >= 0) {
        static_cast<void>(::fsync(kDirectory));
        ::close(kDirectory);
    }
#endif
}

} // namespace rawframe::install
