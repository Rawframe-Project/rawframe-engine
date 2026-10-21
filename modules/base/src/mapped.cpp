#include "rawframe/base/mapped.h"

#if RAWFRAME_FILE_SYSTEM

#include <utility>

#if defined(_WIN32)
// WIN32_LEAN_AND_MEAN and NOMINMAX come from the build, for every file (D237).
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rawframe::base {

std::optional<MappedFile> MappedFile::make(const std::filesystem::path& path, std::size_t size) noexcept {
    if (size == 0) {
        return std::nullopt;
    }
    MappedFile made;
#if defined(_WIN32)
    // Kept in the system's cache where it can be: a temporary file.
    const HANDLE kFile = ::CreateFileW(path.c_str(),
                                       GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr,
                                       CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_TEMPORARY,
                                       nullptr);
    if (kFile == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    made.file_ = kFile;
    const ULARGE_INTEGER kSize{.QuadPart = size};
    made.mapping_ = ::CreateFileMappingW(kFile, nullptr, PAGE_READWRITE, kSize.HighPart, kSize.LowPart, nullptr);
    if (made.mapping_ == nullptr) {
        return std::nullopt;
    }
    made.data_ = static_cast<std::byte*>(::MapViewOfFile(made.mapping_, FILE_MAP_WRITE, 0, 0, size));
#else
    const int kFile = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (kFile < 0) {
        return std::nullopt;
    }
    void* mapped = MAP_FAILED;
    if (::ftruncate(kFile, static_cast<off_t>(size)) == 0) {
        mapped = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, kFile, 0);
    }
    // The mapping keeps the file: its descriptor is not needed again.
    ::close(kFile);
    made.data_ = mapped == MAP_FAILED ? nullptr : static_cast<std::byte*>(mapped);
#endif
    if (made.data_ == nullptr) {
        return std::nullopt;
    }
    made.size_ = size;
    return made;
}

std::optional<MappedFile> MappedFile::open(const std::filesystem::path& path) noexcept {
    MappedFile made;
#if defined(_WIN32)
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
    made.file_ = kFile;
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(kFile, &size) == 0 || size.QuadPart <= 0) {
        return std::nullopt;
    }
    made.mapping_ = ::CreateFileMappingW(kFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (made.mapping_ == nullptr) {
        return std::nullopt;
    }
    made.data_ = static_cast<std::byte*>(::MapViewOfFile(made.mapping_, FILE_MAP_READ, 0, 0, 0));
    made.size_ = static_cast<std::size_t>(size.QuadPart);
#else
    const int kFile = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (kFile < 0) {
        return std::nullopt;
    }
    struct stat about{};
    void* mapped = MAP_FAILED;
    if (::fstat(kFile, &about) == 0 && about.st_size > 0) {
        made.size_ = static_cast<std::size_t>(about.st_size);
        mapped = ::mmap(nullptr, made.size_, PROT_READ, MAP_SHARED, kFile, 0);
    }
    ::close(kFile);
    made.data_ = mapped == MAP_FAILED ? nullptr : static_cast<std::byte*>(mapped);
#endif
    if (made.data_ == nullptr) {
        return std::nullopt;
    }
    return made;
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_{std::exchange(other.data_, nullptr)}, size_{std::exchange(other.size_, 0)}
#if defined(_WIN32)
      ,
      file_{std::exchange(other.file_, nullptr)}, mapping_{std::exchange(other.mapping_, nullptr)}
#endif
{
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        release();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
#if defined(_WIN32)
        file_ = std::exchange(other.file_, nullptr);
        mapping_ = std::exchange(other.mapping_, nullptr);
#endif
    }
    return *this;
}

MappedFile::~MappedFile() {
    release();
}

void MappedFile::release() noexcept {
#if defined(_WIN32)
    if (data_ != nullptr) {
        ::UnmapViewOfFile(data_);
    }
    if (mapping_ != nullptr) {
        ::CloseHandle(mapping_);
    }
    if (file_ != nullptr) {
        ::CloseHandle(file_);
    }
    file_ = nullptr;
    mapping_ = nullptr;
#else
    if (data_ != nullptr) {
        ::munmap(data_, size_);
    }
#endif
    data_ = nullptr;
    size_ = 0;
}

} // namespace rawframe::base

#endif
