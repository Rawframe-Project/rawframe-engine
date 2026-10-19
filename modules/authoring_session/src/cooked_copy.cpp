#include "rawframe/authoring_session/cooked_copy.h"

#include "cooked_files.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/base/files.h"
#include "rawframe/base/sha256.h"
#include "rawframe/content/identity.h"
#include "rawframe/content/manifest.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace rawframe::authoring_session {

namespace {

using document::Value;

std::unexpected<result::Error> refused(std::string_view why) {
    return std::unexpected{result::fail(result::ErrorClass::FailedPrecondition,
                                        authoring::kAuthoringDomain,
                                        authoring::code(authoring::AuthoringError::ValidationFailed),
                                        why)
                               .error()};
}

/// One piece of a cooked file: its bytes, and the file's whole size.
struct Piece {
    std::vector<std::byte> bytes;
    std::uint64_t size = 0;
};

result::Result<Piece> pieceOf(const AskSession& ask, std::string_view output, std::string_view file, std::uint64_t at) {
    Value record = Value::object();
    record.add("kind", Value::string("authoring.cooked_file"));
    record.add("output", Value::string(std::string{output}));
    record.add("file", Value::string(std::string{file}));
    record.add("offset", Value::integer(static_cast<std::int64_t>(at)));
    const std::string kReply = ask(std::move(record));
    const auto kParsed = document::parse(kReply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    if (kAnswer == nullptr) {
        const Value* kError = kParsed.has_value() ? kParsed->find("error") : nullptr;
        const Value* kMessage = kError != nullptr ? kError->find("message") : nullptr;
        return refused(kMessage != nullptr && kMessage->text() != nullptr ? *kMessage->text()
                                                                          : "the session gave no answer");
    }
    // A piece of what was asked, no larger than a piece is, inside the
    // file, and something unless the file has ended.
    const Value* kSize = kAnswer->find("size");
    const Value* kOffset = kAnswer->find("offset");
    const Value* kBytes = kAnswer->find("bytes");
    const std::optional<std::int64_t> kWhole = kSize != nullptr ? kSize->integer() : std::nullopt;
    const std::optional<std::int64_t> kFrom = kOffset != nullptr ? kOffset->integer() : std::nullopt;
    std::optional<std::vector<std::byte>> bytes =
        kBytes != nullptr && kBytes->text() != nullptr ? bytesOfBase64(*kBytes->text()) : std::nullopt;
    if (!kWhole.has_value() || *kWhole < 0 || kFrom != static_cast<std::int64_t>(at) || !bytes.has_value() ||
        bytes->size() > kCookedPiece || at + bytes->size() > static_cast<std::uint64_t>(*kWhole) ||
        (bytes->empty() && at != static_cast<std::uint64_t>(*kWhole))) {
        return refused("a cooked file's piece does not hold together");
    }
    return Piece{.bytes = std::move(*bytes), .size = static_cast<std::uint64_t>(*kWhole)};
}

/// Whether `path` holds the bytes an entry gives: its length and digest.
bool holds(const std::filesystem::path& path, const content::ManifestEntry& entry) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) ||
        std::filesystem::file_size(path, error) != entry.byteLength || error) {
        return false;
    }
    std::ifstream read{path, std::ios::binary};
    base::Sha256 hash;
    std::vector<std::byte> block(std::size_t{64} * 1024);
    while (read) {
        read.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()));
        hash.update(std::span{block.data(), static_cast<std::size_t>(read.gcount())});
    }
    return content::sameDigest(content::ContentDigest{.bytes = hash.finish()}, entry.digest);
}

} // namespace

result::Result<CopiedCook>
copyCooked(const AskSession& ask, std::string_view output, const std::filesystem::path& into) {
    // The manifest first, whole, within the manifest's own limit: nothing
    // is made here for a cook the session does not have.
    std::string manifest;
    std::uint64_t whole = 0;
    do {
        RAWFRAME_TRY_ASSIGN(const Piece kPiece, pieceOf(ask, output, "content.manifest", manifest.size()));
        if (kPiece.size > content::ManifestLimits{}.maximumBytes || (!manifest.empty() && kPiece.size != whole)) {
            return refused("the cook's manifest does not hold together");
        }
        whole = kPiece.size;
        manifest.append(reinterpret_cast<const char*>(kPiece.bytes.data()), kPiece.bytes.size());
    } while (manifest.size() < whole);
    RAWFRAME_TRY_ASSIGN(const std::vector<content::ManifestEntry> kEntries, content::readManifest(manifest));
    std::error_code error;
    std::filesystem::create_directories(into, error);
    if (error || !std::filesystem::is_directory(into, error)) {
        return refused("the cook's copy cannot be made there");
    }
    // Each file once: entries sharing one give it alike.
    CopiedCook made;
    std::map<std::string, const content::ManifestEntry*, std::less<>> located;
    for (const content::ManifestEntry& kEntry : kEntries) {
        const auto [kAt, kNew] = located.emplace(kEntry.locator, &kEntry);
        if (!kNew) {
            if (kAt->second->byteLength != kEntry.byteLength || !(kAt->second->digest == kEntry.digest)) {
                return refused("two of the cook's entries give one file differently");
            }
            continue;
        }
        const std::filesystem::path kPath = into / kEntry.locator;
        if (holds(kPath, kEntry)) {
            ++made.kept;
            continue;
        }
        std::filesystem::create_directories(kPath.parent_path(), error);
        std::filesystem::path partial = kPath;
        partial += ".partial";
        base::Sha256 hash;
        std::uint64_t at = 0;
        {
            std::ofstream write{partial, std::ios::binary | std::ios::trunc};
            do {
                auto piece = pieceOf(ask, output, kEntry.locator, at);
                if (!piece.has_value() || piece->size != kEntry.byteLength) {
                    write.close();
                    std::filesystem::remove(partial, error);
                    return piece.has_value() ? refused("a cooked file is not as its manifest gives it")
                                             : std::unexpected{std::move(piece.error())};
                }
                hash.update(piece->bytes);
                write.write(reinterpret_cast<const char*>(piece->bytes.data()),
                            static_cast<std::streamsize>(piece->bytes.size()));
                at += piece->bytes.size();
            } while (at < kEntry.byteLength);
            write.flush();
            if (!write) {
                std::filesystem::remove(partial, error);
                return refused("the cook's copy cannot be written");
            }
        }
        if (!content::sameDigest(content::ContentDigest{.bytes = hash.finish()}, kEntry.digest)) {
            std::filesystem::remove(partial, error);
            return refused("a cooked file is not as its manifest gives it");
        }
        std::filesystem::rename(partial, kPath, error);
        if (error) {
            std::filesystem::remove(partial, error);
            return refused("the cook's copy cannot be written");
        }
        ++made.copied;
        made.bytes += at;
    }
    if (!base::replaceFile(into / "content.manifest", std::as_bytes(std::span{manifest.data(), manifest.size()}))) {
        return refused("the cook's copy cannot be written");
    }
    return made;
}

} // namespace rawframe::authoring_session
