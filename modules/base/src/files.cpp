#include "rawframe/base/files.h"

#if RAWFRAME_FILE_SYSTEM

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32)
// WIN32_LEAN_AND_MEAN and NOMINMAX come from the build, for every file.
#include <io.h>
#include <windows.h>
#endif

namespace rawframe::base {

namespace {

#if defined(__unix__) || defined(__APPLE__)
/// `bytes` written to the file `path` opened with `flags`, never through a
/// link at it, and flushed to the disk.
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

#if defined(_WIN32)
std::optional<std::string> readWholeFile(const std::filesystem::path& path) {
    // Shared for deletion too, so moveOver can put a new file at `path`
    // while this one reads the old; a stream shares no deletion.
    const HANDLE kFile = ::CreateFileW(path.c_str(),
                                       GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr,
                                       OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
    if (kFile == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    constexpr DWORD kBlock = DWORD{1} << 20U;
    std::string text;
    if (LARGE_INTEGER size{}; ::GetFileSizeEx(kFile, &size) != 0) {
        text.reserve(static_cast<std::size_t>(size.QuadPart) + kBlock);
    }
    bool read = true;
    for (;;) {
        const std::size_t kHad = text.size();
        text.resize(kHad + kBlock);
        DWORD got = 0;
        read = ::ReadFile(kFile, text.data() + kHad, kBlock, &got, nullptr) != 0;
        text.resize(kHad + got);
        if (!read || got == 0) {
            break;
        }
    }
    ::CloseHandle(kFile);
    if (!read) {
        return std::nullopt;
    }
    return text;
}
#else
std::optional<std::string> readWholeFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    constexpr std::size_t kBlock = std::size_t{1} << 20U;
    std::string text;
    std::error_code error;
    if (const auto kSize = std::filesystem::file_size(path, error); !error) {
        text.reserve(static_cast<std::size_t>(kSize) + kBlock);
    }
    // Until a read comes short: what the file holds then, if it grew or
    // shrank since its size was asked.
    for (;;) {
        const std::size_t kHad = text.size();
        text.resize(kHad + kBlock);
        file.read(text.data() + kHad, static_cast<std::streamsize>(kBlock));
        text.resize(kHad + static_cast<std::size_t>(file.gcount()));
        if (!file) {
            break;
        }
    }
    if (file.bad()) {
        return std::nullopt;
    }
    return text;
}
#endif

bool moveOver(const std::filesystem::path& from, const std::filesystem::path& to) noexcept {
#if defined(_WIN32)
    // With POSIX semantics, which a classic move lacks: that one is refused
    // while any handle has `to` open, so a reader of a picture streamed a
    // frame at a time lost the writer one frame in a few. Where the
    // volume cannot (before Windows 10 1809, or not NTFS), the classic one.
    // Named whole: a name alone would be taken as one in `from`'s directory.
    std::error_code absolute;
    const std::wstring kTo = std::filesystem::absolute(to, absolute).native();
    const HANDLE kFile = absolute ? INVALID_HANDLE_VALUE
                                  : ::CreateFileW(from.c_str(),
                                                  DELETE,
                                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                  nullptr,
                                                  OPEN_EXISTING,
                                                  FILE_ATTRIBUTE_NORMAL,
                                                  nullptr);
    if (kFile != INVALID_HANDLE_VALUE) {
        const std::size_t kNameBytes = kTo.size() * sizeof(wchar_t);
        std::string info(offsetof(FILE_RENAME_INFO, FileName) + kNameBytes + sizeof(wchar_t), '\0');
        // FileRenameInfoEx's flags sit where ReplaceIfExists does: replace
        // (1) with POSIX semantics (2).
        constexpr DWORD kFlags = 0x1U | 0x2U;
        std::memcpy(info.data(), &kFlags, sizeof(kFlags));
        const auto kLength = static_cast<DWORD>(kNameBytes);
        std::memcpy(info.data() + offsetof(FILE_RENAME_INFO, FileNameLength), &kLength, sizeof(kLength));
        std::memcpy(info.data() + offsetof(FILE_RENAME_INFO, FileName), kTo.data(), kNameBytes);
        constexpr auto kRenameInfoEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);
        const bool kMoved =
            ::SetFileInformationByHandle(kFile, kRenameInfoEx, info.data(), static_cast<DWORD>(info.size())) != 0;
        ::CloseHandle(kFile);
        if (kMoved) {
            return true;
        }
    }
#endif
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return !error;
}

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
    // never written through to what a link names.
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
    bool moved = moveOver(partial, path);
    for (int attempt = 1; !moved && attempt < 50; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        moved = moveOver(partial, path);
    }
    if (!moved) {
        std::filesystem::remove(partial, error);
        return false;
    }
    flushDirectory(path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."});
    return true;
}

} // namespace rawframe::base

#endif
