#include "cooked_files.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/content/manifest.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace rawframe::authoring_session {

namespace {

using document::Value;

std::unexpected<result::Error> refused(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return std::unexpected{result::fail(kind, authoring::kAuthoringDomain, authoring::code(error), why).error()};
}

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// A Base64 digit's value, or 64 for a character that is none.
std::uint8_t valueOf(char digit) {
    const std::size_t kAt = kAlphabet.find(digit);
    return kAt == std::string_view::npos ? 64 : static_cast<std::uint8_t>(kAt);
}

} // namespace

std::string base64Of(std::span<const std::byte> bytes) {
    std::string made;
    made.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t at = 0; at < bytes.size(); at += 3) {
        const std::size_t kHere = std::min<std::size_t>(3, bytes.size() - at);
        std::uint32_t group = 0;
        for (std::size_t each = 0; each < 3; ++each) {
            group = (group << 8U) | (each < kHere ? std::to_integer<std::uint32_t>(bytes[at + each]) : 0U);
        }
        for (std::size_t each = 0; each < 4; ++each) {
            made += each <= kHere ? kAlphabet[(group >> (18U - (6U * each))) & 63U] : '=';
        }
    }
    return made;
}

std::optional<std::vector<std::byte>> bytesOfBase64(std::string_view text) {
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::byte> made;
    made.reserve(text.size() / 4 * 3);
    for (std::size_t at = 0; at < text.size(); at += 4) {
        // Padding only at the very end, one or two.
        const bool kLast = at + 4 == text.size();
        const std::size_t kPadding = !kLast ? 0 : (text[at + 3] == '=' ? (text[at + 2] == '=' ? 2 : 1) : 0);
        std::uint32_t group = 0;
        for (std::size_t each = 0; each < 4; ++each) {
            const std::uint8_t kValue = each < 4 - kPadding ? valueOf(text[at + each]) : 0;
            if (kValue == 64) {
                return std::nullopt;
            }
            group = (group << 6U) | kValue;
        }
        for (std::size_t each = 0; each < 3 - kPadding; ++each) {
            made.push_back(static_cast<std::byte>((group >> (16U - (8U * each))) & 255U));
        }
    }
    return made;
}

result::Result<Value> cookedFileOf(std::span<const std::filesystem::path> cooked,
                                   const std::optional<std::filesystem::path>& cooking,
                                   const std::filesystem::path& output,
                                   std::string_view file,
                                   std::uint64_t offset) {
    // Only what this session cooked: a record names no other file on the
    // machine.
    std::error_code error;
    const std::filesystem::path kOutput = std::filesystem::weakly_canonical(output, error);
    if (error || std::ranges::find(cooked, kOutput) == cooked.end()) {
        return refused(authoring::AuthoringError::TargetNotFound,
                       result::ErrorClass::NotFound,
                       "no cook in this session wrote there");
    }
    if (cooking.has_value() && *cooking == kOutput) {
        return refused(authoring::AuthoringError::UnsupportedOperation,
                       result::ErrorClass::Unavailable,
                       "a cook is writing there; ask again when it has ended");
    }
    if (file != "content.manifest" && !content::validLocator(file)) {
        return refused(authoring::AuthoringError::ValidationFailed,
                       result::ErrorClass::InvalidArgument,
                       "a cooked file is content.manifest or a locator its manifest gives");
    }
    // A regular file, never a link the cook did not make.
    const std::filesystem::path kPath = kOutput / file;
    const std::filesystem::file_status kStatus = std::filesystem::symlink_status(kPath, error);
    if (error || !std::filesystem::is_regular_file(kStatus)) {
        return refused(
            authoring::AuthoringError::TargetNotFound, result::ErrorClass::NotFound, "the cook wrote no such file");
    }
    const std::uintmax_t kSize = std::filesystem::file_size(kPath, error);
    if (error || offset > kSize) {
        return refused(authoring::AuthoringError::ValidationFailed,
                       result::ErrorClass::InvalidArgument,
                       "an offset is within the file");
    }
    std::vector<std::byte> piece(static_cast<std::size_t>(std::min<std::uintmax_t>(kCookedPiece, kSize - offset)));
    std::ifstream read{kPath, std::ios::binary};
    read.seekg(static_cast<std::streamoff>(offset));
    read.read(reinterpret_cast<char*>(piece.data()), static_cast<std::streamsize>(piece.size()));
    if (!read && !piece.empty()) {
        return refused(authoring::AuthoringError::Internal, result::ErrorClass::Unavailable, "the file cannot be read");
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.cooked_file"));
    made.add("file", Value::string(std::string{file}));
    made.add("size", Value::integer(static_cast<std::int64_t>(kSize)));
    made.add("offset", Value::integer(static_cast<std::int64_t>(offset)));
    made.add("bytes", Value::string(base64Of(piece)));
    return made;
}

} // namespace rawframe::authoring_session
