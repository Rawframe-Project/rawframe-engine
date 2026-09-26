#pragma once

// A library's files as this module reads and writes them: whole, within a
// ceiling, and written durably before they are renamed into place.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::install {

/// A regular file's bytes, at most `ceiling` of them; `absent` is the
/// refusal's words (`FetchFailed`) when there is no such file within it.
[[nodiscard]] result::Result<std::vector<std::byte>>
readFile(const std::filesystem::path& path, std::uint64_t ceiling, std::string_view absent);

/// Writes `bytes` to `staged`, flushed to the disk, then renames it to
/// `path` and flushes the directory, so the file is whole or absent after a
/// crash. Refused (`WriteFailed`) otherwise, `staged` removed.
[[nodiscard]] result::Status
publishFile(const std::filesystem::path& staged, const std::filesystem::path& path, std::span<const std::byte> bytes);

/// Flushes a directory, so a rename in it survives a crash.
void flushDirectory(const std::filesystem::path& directory);

} // namespace rawframe::install
