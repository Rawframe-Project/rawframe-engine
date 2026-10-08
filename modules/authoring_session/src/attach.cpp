#include "rawframe/authoring_session/attach.h"

#include "rawframe/base/sha256.h"
#include "rawframe/document/json.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rawframe::authoring_session {

namespace {

using document::Value;

/// A record's file at most.
constexpr std::size_t kMostRecord = 4096;

std::string textOf(const Value& object, std::string_view name) {
    const Value* kMember = object.find(name);
    return kMember != nullptr && kMember->text() != nullptr ? *kMember->text() : std::string{};
}

} // namespace

std::filesystem::path playDirectoryOf(const std::filesystem::path& game) {
    std::error_code error;
    const std::filesystem::path kGame = std::filesystem::absolute(game, error).lexically_normal();
    const base::Sha256Digest kDigest = base::sha256(kGame.generic_string());
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string name = "rawframe-play-";
    // Half the digest names it: enough that two games never share one.
    for (std::size_t at = 0; at < kDigest.size() / 2; ++at) {
        const auto kByte = static_cast<unsigned>(kDigest[at]);
        name += kDigits[kByte >> 4U];
        name += kDigits[kByte & 0xFU];
    }
    // A directory of the user's own, never a shared one another user could
    // make the game's first: the runtime directory, else the cache. Windows'
    // temporary directory is the user's.
    for (const char* kBase : {"XDG_RUNTIME_DIR", "XDG_CACHE_HOME"}) {
        const char* const kAt = std::getenv(kBase);
        if (kAt != nullptr && *kAt == '/') {
            return std::filesystem::path{kAt} / name;
        }
    }
#if defined(_WIN32)
    return std::filesystem::temp_directory_path(error) / name;
#else
    const char* const kHome = std::getenv("HOME");
    return std::filesystem::path{kHome != nullptr && *kHome == '/' ? kHome : "/nonexistent"} / ".cache" / name;
#endif
}

std::string attachText(const AttachRecord& record) {
    Value made = Value::object();
    made.add("endpoint", Value::string(record.endpoint));
    made.add("pinFile", Value::string(record.pinFile));
    made.add("tokenFile", Value::string(record.tokenFile));
    return document::writeCompact(made) + "\n";
}

std::optional<AttachRecord> attachIn(const std::filesystem::path& directory, std::string& why) {
    std::error_code error;
    const auto kStatus = std::filesystem::symlink_status(directory, error);
    if (error || !std::filesystem::is_directory(kStatus)) {
        why = "the game is not being played here: no play directory";
        return std::nullopt;
    }
#if !defined(_WIN32)
    // Windows keeps the user's temporary directory to the user by its ACL,
    // and std::filesystem reports every permission bit set there, so the
    // bits say nothing of it (D501).
    using std::filesystem::perms;
    if ((kStatus.permissions() & (perms::group_all | perms::others_all)) != perms::none) {
        why = "the play directory is open to others, so not trusted";
        return std::nullopt;
    }
    struct stat held{};
    if (::lstat(directory.c_str(), &held) != 0 || held.st_uid != ::getuid()) {
        why = "the play directory is not this user's, so not trusted";
        return std::nullopt;
    }
#endif
    const std::filesystem::path kFile = directory / kAttachFile;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(kFile, error))) {
        why = "the game is not being played: no attach record";
        return std::nullopt;
    }
    std::string text(kMostRecord + 1, '\0');
    std::FILE* file = std::fopen(kFile.string().c_str(), "rb");
    if (file == nullptr) {
        why = "the attach record cannot be read";
        return std::nullopt;
    }
    text.resize(std::fread(text.data(), 1, text.size(), file));
    std::fclose(file);
    const auto kParsed = document::parse(text.size() <= kMostRecord ? std::string_view{text} : std::string_view{});
    if (!kParsed.has_value() || kParsed->kind() != Value::Kind::Object) {
        why = "the attach record is not one";
        return std::nullopt;
    }
    AttachRecord record{.endpoint = textOf(*kParsed, "endpoint"),
                        .pinFile = textOf(*kParsed, "pinFile"),
                        .tokenFile = textOf(*kParsed, "tokenFile")};
    if (record.endpoint.empty() || record.pinFile.empty() || record.tokenFile.empty()) {
        why = "the attach record names no endpoint, pin file, and token file";
        return std::nullopt;
    }
    return record;
}

} // namespace rawframe::authoring_session
