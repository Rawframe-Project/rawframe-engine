#include "rawframe/world_save/store.h"

#include "rawframe/execution/executor.h"
#include "rawframe/world_save/errors.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rawframe::world_save {

namespace {

constexpr std::string_view kSuffix = ".rfsave";
constexpr std::size_t kMaximumSlot = 64;

std::unexpected<result::Error> failed(result::ErrorClass errorClass, SaveError error, std::string_view why) {
    return result::fail(errorClass, kSaveDomain, code(error), why);
}

result::Status checkSlot(std::string_view slot) {
    const bool kValid = !slot.empty() && slot.size() <= kMaximumSlot && std::ranges::all_of(slot, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
               character == '-';
    });
    if (!kValid) {
        return failed(result::ErrorClass::InvalidArgument,
                      SaveError::InvalidSlot,
                      "a save slot is 1 to 64 of a-z, 0-9, _, and -");
    }
    return {};
}

/// Writes and flushes a whole file.
bool writeWhole(const std::string& path, std::span<const std::byte> bytes) {
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
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool kFlushed = std::fflush(file) == 0;
    return std::fclose(file) == 0 && kWritten && kFlushed;
#endif
}

/// Flushes the directory, so a rename in it survives a crash.
void flushDirectory([[maybe_unused]] const std::filesystem::path& directory) {
#if defined(__unix__) || defined(__APPLE__)
    const int kDirectory = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (kDirectory >= 0) {
        static_cast<void>(::fsync(kDirectory));
        ::close(kDirectory);
    }
#endif
}

} // namespace

result::Result<std::vector<std::byte>> DirectorySaveStore::load(std::string_view slot, std::size_t maximumBytes) {
    RAWFRAME_TRY(execution::requireBlockingAllowed());
    RAWFRAME_TRY(checkSlot(slot));
    const std::filesystem::path kPath = directory_ / (std::string{slot} + std::string{kSuffix});
    std::FILE* file = std::fopen(kPath.string().c_str(), "rb");
    if (file == nullptr) {
        return failed(result::ErrorClass::NotFound, SaveError::Absent, "no save is kept under this slot");
    }
    std::vector<std::byte> bytes;
    std::array<std::byte, 65536> chunk{};
    std::size_t got = 0;
    while ((got = std::fread(chunk.data(), 1, chunk.size(), file)) != 0 && bytes.size() <= maximumBytes) {
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
    }
    const bool kFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (kFailed) {
        return failed(result::ErrorClass::Unavailable, SaveError::StorageFailed, "a kept save cannot be read");
    }
    if (bytes.size() > maximumBytes) {
        return failed(
            result::ErrorClass::ResourceExhausted, SaveError::LimitExceeded, "a kept save is larger than its limit");
    }
    return bytes;
}

result::Status DirectorySaveStore::keep(std::string_view slot, std::span<const std::byte> bytes) {
    RAWFRAME_TRY(execution::requireBlockingAllowed());
    RAWFRAME_TRY(checkSlot(slot));
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    const std::filesystem::path kPath = directory_ / (std::string{slot} + std::string{kSuffix});
    const std::string kPartial = kPath.string() + ".partial";
    if (!writeWhole(kPartial, bytes)) {
        std::remove(kPartial.c_str());
        return failed(result::ErrorClass::Unavailable, SaveError::StorageFailed, "a save cannot be written");
    }
    std::filesystem::rename(kPartial, kPath, error);
    if (error) {
        std::remove(kPartial.c_str());
        return failed(
            result::ErrorClass::Unavailable, SaveError::StorageFailed, "a written save cannot be put in place");
    }
    flushDirectory(directory_);
    return {};
}

} // namespace rawframe::world_save
