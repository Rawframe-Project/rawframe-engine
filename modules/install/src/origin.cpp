#include "rawframe/install/origin.h"

#include "files.h"
#include "rawframe/content/library.h"
#include "rawframe/install/errors.h"

namespace rawframe::install {

namespace {

std::unexpected<result::Error> unfetched(std::string_view why, const std::filesystem::path& path) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kInstallDomain, code(InstallError::FetchFailed), why)
            .error()
            .withContext("path", path.generic_string())};
}

/// A directory laid out as a library, or holding one packed Build.
class DirectoryOrigin final : public Origin {
public:
    DirectoryOrigin(std::filesystem::path root, bool packed) noexcept : root_(std::move(root)), packed_(packed) {
    }

    result::Result<std::vector<std::byte>> manifest(const base::Sha256Digest& root, std::uint64_t ceiling) override {
        return readFile(buildDirectory(root) / content::kBuildManifestName, ceiling, "the origin has no such manifest");
    }

    result::Result<std::vector<std::byte>> signature(const base::Sha256Digest& root, std::uint64_t ceiling) override {
        return readFile(
            buildDirectory(root) / content::kBuildSignatureName, ceiling, "the origin has no such signature");
    }

    result::Result<std::vector<std::byte>> blob(const content::ContentDigest& blob, std::uint64_t ceiling) override {
        return readFile(root_ / content::blobPathOf(blob), ceiling, "the origin has no such blob");
    }

private:
    [[nodiscard]] std::filesystem::path buildDirectory(const base::Sha256Digest& root) const {
        return packed_ ? root_ : root_ / content::buildDirectoryOf(root);
    }

    std::filesystem::path root_;
    bool packed_ = false;
};

result::Result<std::unique_ptr<Origin>> directoryOrigin(const std::filesystem::path& root, bool packed) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return unfetched("the origin is not a directory", root);
    }
    return std::unique_ptr<Origin>{std::make_unique<DirectoryOrigin>(root, packed)};
}

} // namespace

result::Result<std::unique_ptr<Origin>> mirrorAt(const std::filesystem::path& root) {
    return directoryOrigin(root, false);
}

result::Result<std::unique_ptr<Origin>> packedBuildAt(const std::filesystem::path& root) {
    return directoryOrigin(root, true);
}

} // namespace rawframe::install
