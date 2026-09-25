#include "rawframe/composition/held_files.h"

#include "rawframe/composition/errors.h"

#include <algorithm>

namespace rawframe::composition {

namespace {

bool wellFormed(std::string_view path) noexcept {
    if (path.empty()) {
        return false;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t kEnd = std::min(path.find('/', start), path.size());
        const std::string_view kPart = path.substr(start, kEnd - start);
        if (kPart.empty() || kPart == "." || kPart == "..") {
            return false;
        }
        if (kEnd == path.size()) {
            return true;
        }
        start = kEnd + 1;
    }
}

} // namespace

result::Result<HeldFiles> HeldFiles::of(std::vector<File> files) {
    std::ranges::sort(files, {}, &File::first);
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (!wellFormed(files[i].first) || (i > 0 && files[i].first == files[i - 1].first)) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kCompositionDomain,
                                code(CompositionError::BadConfiguration),
                                "a held file's path is empty, not relative, or held twice");
        }
    }
    HeldFiles held;
    held.files_ = std::move(files);
    return held;
}

const std::vector<std::byte>* HeldFiles::find(std::string_view path) const noexcept {
    const auto kAt = std::ranges::lower_bound(files_, path, {}, &File::first);
    return kAt != files_.end() && kAt->first == path ? &kAt->second : nullptr;
}

std::vector<HeldFiles::File> HeldFiles::under(std::string_view directory) const {
    std::vector<File> found;
    const std::string kPrefix = directory.empty() ? std::string{} : std::string{directory} + "/";
    for (auto at = std::ranges::lower_bound(files_, kPrefix, {}, &File::first);
         at != files_.end() && at->first.starts_with(kPrefix);
         ++at) {
        found.emplace_back(at->first.substr(kPrefix.size()), at->second);
    }
    return found;
}

} // namespace rawframe::composition
