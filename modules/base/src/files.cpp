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

namespace {

#if defined(__unix__) || defined(__APPLE__)
/// `bytes` written to the file `path` opened with `flags`, never through a
/// link at it (D847), and flushed to the disk.
bool writeOpened(const std::filesystem::path& path, std::span<const std::byte> bytes, int flags) noexcept {
    const int kFile = ::open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW | flags, 0644);
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
}
#endif

} // namespace

bool writeWholeFile(const std::filesystem::path& path, std::span<const std::byte> bytes) noexcept {
#if defined(__unix__) || defined(__APPLE__)
    return writeOpened(path, bytes, O_CREAT | O_TRUNC);
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
    // What is at the partial file's place, one a crash left or a link a
    // directory came with, is taken away, and the partial file made new:
    // never written through to what a link names (D847).
    std::filesystem::remove(partial, error);
#if defined(__unix__) || defined(__APPLE__)
    const bool kWritten = writeOpened(partial, bytes, O_CREAT | O_EXCL);
#else
    const bool kWritten = writeWholeFile(partial, bytes);
#endif
    if (!kWritten) {
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
