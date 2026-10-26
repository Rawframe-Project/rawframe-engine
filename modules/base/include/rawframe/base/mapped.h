#pragma once

// A file mapped into memory, written or read in place: what one
// process writes there another sees without a file made, closed, or read
// again, so nothing scans it each time as Windows' real-time protection
// scans a file written whole. Where there are files (RAWFRAME_FILE_SYSTEM)
// only.

#include "rawframe/base/platform.h"

#include <cstddef>
#include <optional>
#include <span>

#if RAWFRAME_FILE_SYSTEM

#include <filesystem>

namespace rawframe::base {

class MappedFile {
public:
    /// The file at `path` made new, `size` bytes of nought, and mapped to
    /// write; none where it cannot be. Opened sharing its deletion, so
    /// `moveOver` can move it, or put another in its place, while mapped.
    [[nodiscard]] static std::optional<MappedFile> make(const std::filesystem::path& path, std::size_t size) noexcept;
    /// The file at `path` mapped whole to read, sharing its deletion too;
    /// none where it is not there, is empty, or cannot be mapped.
    [[nodiscard]] static std::optional<MappedFile> open(const std::filesystem::path& path) noexcept;

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    ~MappedFile();

    /// Its bytes, at the start of a page; written only where it was made.
    [[nodiscard]] std::span<std::byte> bytes() noexcept {
        return {data_, size_};
    }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {data_, size_};
    }

private:
    MappedFile() = default;
    void release() noexcept;

    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
#if defined(_WIN32)
    void* file_ = nullptr;
    void* mapping_ = nullptr;
#endif
};

} // namespace rawframe::base

#endif
