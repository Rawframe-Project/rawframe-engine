#include "rawframe/base/files.h"

#if RAWFRAME_FILE_SYSTEM

#include <chrono>
#include <cstdio>
#include <string>
#include <system_error>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h>
#endif

namespace rawframe::base {

bool writeWholeFile(const std::filesystem::path& path, std::span<const std::byte> bytes) noexcept {
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

void flushDirectory([[maybe_unused]] const std::filesystem::path& directory) noexcept {
#if defined(__unix__) || defined(__APPLE__)
    const int kDirectory = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (kDirectory >= 0) {
        static_cast<void>(::fsync(kDirectory));
        ::close(kDirectory);
    }
#endif
}

bool replaceFile(const std::filesystem::path& path, std::span<const std::byte> bytes) noexcept {
    std::filesystem::path partial = path;
    partial += ".partial";
    std::error_code error;
    if (!writeWholeFile(partial, bytes)) {
        std::filesystem::remove(partial, error);
        return false;
    }
    for (int attempt = 0; attempt < 50; ++attempt) {
        error.clear();
        std::filesystem::rename(partial, path, error);
        if (!error) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (error) {
        std::error_code removed;
        std::filesystem::remove(partial, removed);
        return false;
    }
    flushDirectory(path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."});
    return true;
}

} // namespace rawframe::base

#endif
