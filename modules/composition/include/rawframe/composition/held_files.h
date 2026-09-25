#pragma once

// Files a host holds in memory by their paths: what a web client fetched,
// where there is no file system (RAWFRAME_FILE_SYSTEM), or what a test
// holds. A host that holds files hands them to its Composition, and a path
// the configuration names is then read from them, never from a disk (D167).

#include "rawframe/result/result.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::composition {

class HeldFiles {
public:
    /// A file's path, parts between `/`, and its bytes.
    using File = std::pair<std::string, std::vector<std::byte>>;

    HeldFiles() = default;

    /// Refuses (`BadConfiguration`) a path that is empty, has an empty,
    /// `.`, or `..` part (so none starts with `/`), or is held twice.
    [[nodiscard]] static result::Result<HeldFiles> of(std::vector<File> files);

    /// The bytes held at `path`, or null.
    [[nodiscard]] const std::vector<std::byte>* find(std::string_view path) const noexcept;
    /// Every file under the directory `directory`, by its path relative to
    /// it, in path order; the empty directory holds every file.
    [[nodiscard]] std::vector<File> under(std::string_view directory) const;
    /// Every file, in path order.
    [[nodiscard]] std::span<const File> files() const noexcept {
        return files_;
    }

private:
    std::vector<File> files_;
};

} // namespace rawframe::composition
