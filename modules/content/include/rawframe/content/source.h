#pragma once

// Where content bytes come from (SPEC-0008): a source reads a validated
// locator's exact bytes and nothing else. Generation 1 has two, both built
// here and nowhere else: a memory source for tests and fixtures, and a
// directory source confined to its root. There is no public way to add
// another kind.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::content {

class ContentStore;

class ContentSource {
public:
    /// Bytes held in memory, by locator. Refuses (`InvalidLocator`) a
    /// locator `validLocator` does not accept.
    [[nodiscard]] static result::Result<ContentSource>
    memory(std::vector<std::pair<std::string, std::vector<std::byte>>> files);

    /// Files under `root`, which must be a directory (`SourceUnavailable`).
    /// A read walks each segment of its locator from the root without
    /// following a link and without crossing onto another filesystem
    /// (`PathEscape`), and lists nothing: only what a catalog names is read.
    [[nodiscard]] static result::Result<ContentSource> directory(const std::filesystem::path& root);

    struct Implementation;

private:
    friend class ContentStore;
    explicit ContentSource(std::shared_ptr<const Implementation> implementation) noexcept
        : implementation_(std::move(implementation)) {
    }
    std::shared_ptr<const Implementation> implementation_;
};

} // namespace rawframe::content
