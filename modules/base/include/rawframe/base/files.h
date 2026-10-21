#pragma once

// Files written so a crash leaves either the old file or the new one, whole
// (D626): written and flushed past the system's cache, then moved over the
// old one, and the move flushed in its directory; and files read whole, in
// blocks (D964). Where there are files (RAWFRAME_FILE_SYSTEM) only.

#include "rawframe/base/platform.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#if RAWFRAME_FILE_SYSTEM

#include <filesystem>

namespace rawframe::base {

/// The whole of the file at `path`, as its bytes; none where it cannot be
/// opened or read. Read in large blocks: a stream's iterator, a character
/// at a time, took a tenth of a second for four megabytes on Windows (D964).
/// Opened so moveOver can put a new file in its place meanwhile (D965).
[[nodiscard]] std::optional<std::string> readWholeFile(const std::filesystem::path& path);

/// The file at `from` moved over the one at `to`, in one step; whether it
/// moved. A reader that has `to` open keeps reading what it opened. On
/// Windows that holds for a reader that opened it through readWholeFile,
/// which shares its deletion; any other open handle refuses the move for
/// as long as it is open (D965).
[[nodiscard]] bool moveOver(const std::filesystem::path& from, const std::filesystem::path& to) noexcept;

/// `bytes` written as the whole of the file at `path`, made or emptied
/// first, and flushed to the disk; whether all of it was. A link at `path`
/// is not written through: refused where the platform can tell (D847).
[[nodiscard]] bool writeWholeFile(const std::filesystem::path& path, std::span<const std::byte> bytes) noexcept;

/// The directory flushed, so a move in it survives a crash; nothing where
/// the platform keeps none of its own.
void flushDirectory(const std::filesystem::path& directory) noexcept;

/// `bytes` put at `path` whole or not at all: written beside it under
/// `.partial`, made new there, then moved over it; a link at `path` is
/// replaced, not followed (D847). A file a handle other than readWholeFile's
/// holds open cannot be replaced on Windows for that moment, so the move is
/// tried again for up to half a second. Whether it is in place; the partial file is gone
/// either way. The directory must exist.
[[nodiscard]] bool replaceFile(const std::filesystem::path& path, std::span<const std::byte> bytes) noexcept;

} // namespace rawframe::base

#endif
