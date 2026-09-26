#pragma once

// A source over a directory (SPEC-0008): files read beneath one root, no link
// followed and no other filesystem entered on the way, and a file that
// changes while it is read refused. POSIX and Windows keep those promises
// each in their own way (D237).

#include "source.h"

#include <filesystem>
#include <memory>

#if RAWFRAME_FILE_SYSTEM

namespace rawframe::content {

class DirectorySource final : public ContentSource::Implementation {
public:
    /// The root, held open. `unavailable` is the refusal's words when it is
    /// not a readable directory.
    static result::Result<std::shared_ptr<const DirectorySource>> open(const std::filesystem::path& root,
                                                                       std::string_view unavailable);

    result::Result<std::vector<std::byte>> read(std::string_view locator, std::uint64_t length) const override;

    /// The size of the regular file at a locator, found as `read` finds it.
    [[nodiscard]] result::Result<std::uint64_t> size(std::string_view locator) const;

    /// The platform's hold on the root.
    struct Root;
    explicit DirectorySource(std::unique_ptr<Root> root) noexcept;
    ~DirectorySource() override;
    DirectorySource(const DirectorySource&) = delete;
    DirectorySource& operator=(const DirectorySource&) = delete;

private:
    std::unique_ptr<Root> root_;
};

} // namespace rawframe::content

#endif
