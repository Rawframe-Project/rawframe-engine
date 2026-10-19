#pragma once

// A cook's output read back a piece at a time (D905): its manifest, or a
// file the manifest locates, so what was cooked on one machine is played on
// another, as a collaborator plays what a shared session's host cooked.

#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

/// The most bytes one piece holds: a reply of a third more as Base64, well
/// inside a host's record of at most 16 MiB.
inline constexpr std::size_t kCookedPiece = std::size_t{192} * 1024;

/// `authoring.cooked_file`'s answer: `file` of the cook into `output`, from
/// `offset`, at most `kCookedPiece` bytes as Base64, with the file's whole
/// size. Only an output one of this session's cooks wrote (`cooked`), and
/// not while one cooks into it, is read: refused otherwise
/// (`target_not_found`, `unsupported_operation` of class `unavailable`).
/// The file is `content.manifest` or a locator (SPEC-0008), a regular file
/// under the output (`validation_failed`, `target_not_found`); an offset
/// past its end is refused too (`validation_failed`).
[[nodiscard]] result::Result<document::Value> cookedFileOf(std::span<const std::filesystem::path> cooked,
                                                           const std::optional<std::filesystem::path>& cooking,
                                                           const std::filesystem::path& output,
                                                           std::string_view file,
                                                           std::uint64_t offset);

/// Bytes as Base64 (RFC 4648, padded), and Base64 read back, none for text
/// that is not Base64.
[[nodiscard]] std::string base64Of(std::span<const std::byte> bytes);
[[nodiscard]] std::optional<std::vector<std::byte>> bytesOfBase64(std::string_view text);

} // namespace rawframe::authoring_session
