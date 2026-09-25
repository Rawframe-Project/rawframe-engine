#include "source.h"

#include "rawframe/content/errors.h"
#include "rawframe/content/manifest.h"

#include <cerrno>
#include <fcntl.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

namespace rawframe::content {

namespace {

std::unexpected<result::Error> refuse(ContentError error, result::ErrorClass errorClass, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kContentDomain, code(error), why).error()};
}

class MemorySource final : public ContentSource::Implementation {
public:
    explicit MemorySource(std::map<std::string, std::vector<std::byte>, std::less<>> files) noexcept
        : files_(std::move(files)) {
    }

    result::Result<std::vector<std::byte>> read(std::string_view locator, std::uint64_t length) const override {
        const auto kFound = files_.find(locator);
        if (kFound == files_.end()) {
            return refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "the source has no such file");
        }
        if (kFound->second.size() < length) {
            return refuse(ContentError::ShortRead, result::ErrorClass::DataLoss, "the file is shorter than declared");
        }
        if (kFound->second.size() > length) {
            return refuse(
                ContentError::SourceChanged, result::ErrorClass::DataLoss, "the file is longer than declared");
        }
        return kFound->second;
    }

private:
    std::map<std::string, std::vector<std::byte>, std::less<>> files_;
};

/// A file descriptor closed when it goes.
class Descriptor {
public:
    explicit Descriptor(int descriptor = -1) noexcept : descriptor_(descriptor) {
    }
    Descriptor(Descriptor&& other) noexcept : descriptor_(std::exchange(other.descriptor_, -1)) {
    }
    Descriptor& operator=(Descriptor&& other) noexcept {
        std::swap(descriptor_, other.descriptor_);
        return *this;
    }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    ~Descriptor() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }
    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }

private:
    int descriptor_;
};

class DirectorySource final : public ContentSource::Implementation {
public:
    DirectorySource(Descriptor root, dev_t device) noexcept : root_(std::move(root)), device_(device) {
    }

    result::Result<std::vector<std::byte>> read(std::string_view locator, std::uint64_t length) const override {
        // Segment by segment from the root: no link is followed and no other
        // filesystem entered, so nothing outside the root is reachable.
        Descriptor directory;
        int at = root_.get();
        std::size_t start = 0;
        for (std::size_t end = locator.find('/'); end != std::string_view::npos; end = locator.find('/', start)) {
            const std::string kSegment{locator.substr(start, end - start)};
            Descriptor next{::openat(at, kSegment.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
            RAWFRAME_TRY(check(next, "a directory on the way"));
            directory = std::move(next);
            at = directory.get();
            start = end + 1;
        }
        const std::string kName{locator.substr(start)};
        const Descriptor kFile{::openat(at, kName.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
        RAWFRAME_TRY(check(kFile, "the file"));
        struct stat before{};
        if (::fstat(kFile.get(), &before) != 0 || !S_ISREG(before.st_mode)) {
            return refuse(ContentError::ReadFailed, result::ErrorClass::Unavailable, "not a regular file");
        }
        if (static_cast<std::uint64_t>(before.st_size) < length) {
            return refuse(ContentError::ShortRead, result::ErrorClass::DataLoss, "the file is shorter than declared");
        }
        if (static_cast<std::uint64_t>(before.st_size) > length) {
            return refuse(
                ContentError::SourceChanged, result::ErrorClass::DataLoss, "the file is longer than declared");
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        std::size_t done = 0;
        while (done < bytes.size()) {
            const ssize_t kRead = ::read(kFile.get(), bytes.data() + done, bytes.size() - done);
            if (kRead < 0 && errno == EINTR) {
                continue;
            }
            if (kRead < 0) {
                return refuse(ContentError::ReadFailed, result::ErrorClass::Unavailable, "the file cannot be read");
            }
            if (kRead == 0) {
                return refuse(ContentError::ShortRead, result::ErrorClass::DataLoss, "the file ended early");
            }
            done += static_cast<std::size_t>(kRead);
        }
        // Changed while it was read: its size or its modification.
        std::byte extra{};
        struct stat after{};
        if (::read(kFile.get(), &extra, 1) != 0 || ::fstat(kFile.get(), &after) != 0 ||
            after.st_size != before.st_size || after.st_mtim.tv_sec != before.st_mtim.tv_sec ||
            after.st_mtim.tv_nsec != before.st_mtim.tv_nsec) {
            return refuse(
                ContentError::SourceChanged, result::ErrorClass::DataLoss, "the file changed while it was read");
        }
        return bytes;
    }

private:
    result::Status check(const Descriptor& opened, std::string_view what) const {
        if (opened.get() < 0) {
            if (errno == ELOOP || errno == ENOTDIR) {
                return refuse(ContentError::PathEscape, result::ErrorClass::PermissionDenied, "a link on the way");
            }
            return std::unexpected<result::Error>{
                refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "cannot be opened")
                    .error()
                    .withContext("what", what)};
        }
        struct stat status{};
        if (::fstat(opened.get(), &status) != 0 || status.st_dev != device_) {
            return refuse(
                ContentError::PathEscape, result::ErrorClass::PermissionDenied, "another filesystem on the way");
        }
        return {};
    }

    Descriptor root_;
    dev_t device_;
};

} // namespace

result::Result<ContentSource> ContentSource::memory(std::vector<std::pair<std::string, std::vector<std::byte>>> files) {
    std::map<std::string, std::vector<std::byte>, std::less<>> held;
    for (auto& [locator, bytes] : files) {
        if (!validLocator(locator)) {
            return refuse(ContentError::InvalidLocator, result::ErrorClass::InvalidArgument, "not a valid locator");
        }
        held.insert_or_assign(std::move(locator), std::move(bytes));
    }
    return ContentSource{std::make_shared<const MemorySource>(std::move(held))};
}

result::Result<ContentSource> ContentSource::directory(const std::filesystem::path& root) {
    Descriptor opened{::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    struct stat status{};
    if (opened.get() < 0 || ::fstat(opened.get(), &status) != 0) {
        return refuse(
            ContentError::SourceUnavailable, result::ErrorClass::Unavailable, "the root is not a readable directory");
    }
    return ContentSource{std::make_shared<const DirectorySource>(std::move(opened), status.st_dev)};
}

} // namespace rawframe::content
