#include "http_origin.h"

#include "rawframe/content/library.h"
#include "rawframe/http/url.h"

#include <filesystem>
#include <string>

namespace rawframe::install_tool {

namespace {

class HttpOrigin final : public install::Origin {
public:
    HttpOrigin(std::string base, std::unique_ptr<http::Client> client) noexcept
        : base_(std::move(base)), client_(std::move(client)) {
    }

    result::Result<std::vector<std::byte>> manifest(const base::Sha256Digest& root, std::uint64_t ceiling) override {
        return fetch(std::filesystem::path{content::buildDirectoryOf(root)} / content::kBuildManifestName, ceiling);
    }

    result::Result<std::vector<std::byte>> signature(const base::Sha256Digest& root, std::uint64_t ceiling) override {
        return fetch(std::filesystem::path{content::buildDirectoryOf(root)} / content::kBuildSignatureName, ceiling);
    }

    result::Result<std::vector<std::byte>> blob(const content::ContentDigest& blob, std::uint64_t ceiling) override {
        return fetch(content::blobPathOf(blob), ceiling);
    }

private:
    /// The library's own relative path, below the base: the same names
    /// content::Library gives a library on disk.
    result::Result<std::vector<std::byte>> fetch(const std::filesystem::path& path, std::uint64_t ceiling) {
        return client_->get(base_ + "/" + path.generic_string(), ceiling);
    }

    std::string base_;
    std::unique_ptr<http::Client> client_;
};

} // namespace

bool overHttp(std::string_view text) noexcept {
    return text.starts_with("http://") || text.starts_with("https://");
}

result::Result<std::unique_ptr<install::Origin>> mirrorOver(std::string_view base, http::ClientSettings settings) {
    std::string trimmed{base};
    while (trimmed.ends_with('/')) {
        trimmed.pop_back();
    }
    // Checked now, so a bad URL is refused before anything is fetched.
    RAWFRAME_TRY(http::parseUrl(trimmed));
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<http::Client> client, http::Client::create(std::move(settings)));
    return std::unique_ptr<install::Origin>{std::make_unique<HttpOrigin>(std::move(trimmed), std::move(client))};
}

} // namespace rawframe::install_tool
