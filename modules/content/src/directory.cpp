#include "directory.h"

#include "rawframe/content/errors.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if RAWFRAME_FILE_SYSTEM

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rawframe::content {

namespace {

std::unexpected<result::Error> refuse(ContentError error, result::ErrorClass errorClass, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kContentDomain, code(error), why).error()};
}

std::unexpected<result::Error> linkOnTheWay() {
    return refuse(ContentError::PathEscape, result::ErrorClass::PermissionDenied, "a link on the way");
}

std::unexpected<result::Error> anotherFilesystem() {
    return refuse(ContentError::PathEscape, result::ErrorClass::PermissionDenied, "another filesystem on the way");
}

std::unexpected<result::Error> cannotOpen(std::string_view what) {
    return std::unexpected<result::Error>{
        refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "cannot be opened")
            .error()
            .withContext("what", what)};
}

/// What a file is, to tell whether it changed while it was read.
struct Status {
    bool regular = false;
    std::uint64_t size = 0;
    std::uint64_t modifiedHigh = 0;
    std::uint64_t modifiedLow = 0;

    bool operator==(const Status&) const = default;
};

#if defined(_WIN32)

/// A handle closed when it goes.
class Handle {
public:
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {
    }
    Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {
    }
    Handle& operator=(Handle&& other) noexcept {
        std::swap(handle_, other.handle_);
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
    }
    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_;
};

/// A path Windows takes as it is: no device names such as `nul`, no dots or
/// spaces trimmed from the end of a name, no length limit.
std::wstring extended(const std::filesystem::path& path) {
    std::wstring text = std::filesystem::absolute(path).make_preferred().wstring();
    if (text.starts_with(L"\\\\?\\")) {
        return text;
    }
    if (text.starts_with(L"\\\\")) {
        return L"\\\\?\\UNC\\" + text.substr(2);
    }
    return L"\\\\?\\" + text;
}

/// Opened without following a link; each directory on the way is held open
/// without delete sharing, so it cannot be renamed or replaced meanwhile.
Handle openNoFollow(const std::wstring& path, DWORD access, bool directory) {
    return Handle{::CreateFileW(path.c_str(),
                                access,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT |
                                    (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN),
                                nullptr)};
}

std::optional<BY_HANDLE_FILE_INFORMATION> informationOf(const Handle& handle) {
    BY_HANDLE_FILE_INFORMATION information{};
    if (::GetFileInformationByHandle(handle.get(), &information) == 0) {
        return std::nullopt;
    }
    return information;
}

std::optional<Status> statusOf(const Handle& file) {
    const auto kInformation = informationOf(file);
    if (!kInformation) {
        return std::nullopt;
    }
    constexpr DWORD kNotRegular = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE;
    return Status{.regular = (kInformation->dwFileAttributes & kNotRegular) == 0 &&
                             ::GetFileType(file.get()) == FILE_TYPE_DISK,
                  .size = (static_cast<std::uint64_t>(kInformation->nFileSizeHigh) << 32U) | kInformation->nFileSizeLow,
                  .modifiedHigh = kInformation->ftLastWriteTime.dwHighDateTime,
                  .modifiedLow = kInformation->ftLastWriteTime.dwLowDateTime};
}

/// Bytes read, 0 at the end, negative on failure.
std::int64_t readSome(const Handle& file, std::byte* into, std::size_t most) {
    DWORD got = 0;
    const auto kAsked = static_cast<DWORD>(std::min<std::size_t>(most, DWORD{1} << 30U));
    if (::ReadFile(file.get(), into, kAsked, &got, nullptr) == 0) {
        return -1;
    }
    return got;
}

} // namespace

struct DirectorySource::Root {
    Handle directory;
    std::wstring path;
    DWORD volume = 0;
};

result::Result<std::shared_ptr<const DirectorySource>> DirectorySource::open(const std::filesystem::path& root,
                                                                             std::string_view unavailable) {
    auto held = std::make_unique<Root>();
    held->path = extended(root);
    held->directory = Handle{::CreateFileW(held->path.c_str(),
                                           FILE_READ_ATTRIBUTES,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           nullptr,
                                           OPEN_EXISTING,
                                           FILE_FLAG_BACKUP_SEMANTICS,
                                           nullptr)};
    const auto kInformation = held->directory.valid() ? informationOf(held->directory) : std::nullopt;
    if (!kInformation || (kInformation->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return refuse(ContentError::SourceUnavailable, result::ErrorClass::Unavailable, unavailable);
    }
    held->volume = kInformation->dwVolumeSerialNumber;
    return std::make_shared<const DirectorySource>(std::move(held));
}

namespace {

/// The file at a locator, segment by segment from the root: no link is
/// followed and no other volume entered, so nothing outside the root is
/// reachable.
result::Result<Handle> openBeneath(const DirectorySource::Root& root, std::string_view locator) {
    std::vector<Handle> directories;
    std::wstring path = root.path;
    std::size_t start = 0;
    const auto kStep = [&](std::string_view segment) {
        path += L'\\';
        // Locators are ASCII (validLocator).
        path.append(segment.begin(), segment.end());
    };
    for (std::size_t end = locator.find('/'); end != std::string_view::npos; end = locator.find('/', start)) {
        kStep(locator.substr(start, end - start));
        Handle next = openNoFollow(path, FILE_READ_ATTRIBUTES, true);
        if (!next.valid()) {
            return cannotOpen("a directory on the way");
        }
        const auto kInformation = informationOf(next);
        if (!kInformation || (kInformation->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (kInformation->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return linkOnTheWay();
        }
        if (kInformation->dwVolumeSerialNumber != root.volume) {
            return anotherFilesystem();
        }
        directories.push_back(std::move(next));
        start = end + 1;
    }
    kStep(locator.substr(start));
    Handle file = openNoFollow(path, GENERIC_READ, false);
    if (!file.valid()) {
        return cannotOpen("the file");
    }
    const auto kInformation = informationOf(file);
    if (!kInformation || (kInformation->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return linkOnTheWay();
    }
    if (kInformation->dwVolumeSerialNumber != root.volume) {
        return anotherFilesystem();
    }
    return file;
}

#else

/// A file descriptor closed when it goes.
class Handle {
public:
    explicit Handle(int descriptor = -1) noexcept : descriptor_(descriptor) {
    }
    Handle(Handle&& other) noexcept : descriptor_(std::exchange(other.descriptor_, -1)) {
    }
    Handle& operator=(Handle&& other) noexcept {
        std::swap(descriptor_, other.descriptor_);
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }
    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return descriptor_ >= 0;
    }

private:
    int descriptor_;
};

/// When a file was last modified, by the name each platform gives it.
const timespec& modifiedAt(const struct stat& status) noexcept {
#if defined(__APPLE__)
    return status.st_mtimespec;
#else
    return status.st_mtim;
#endif
}

std::optional<Status> statusOf(const Handle& file) {
    struct stat status{};
    if (::fstat(file.get(), &status) != 0) {
        return std::nullopt;
    }
    return Status{.regular = S_ISREG(status.st_mode),
                  .size = static_cast<std::uint64_t>(status.st_size),
                  .modifiedHigh = static_cast<std::uint64_t>(modifiedAt(status).tv_sec),
                  .modifiedLow = static_cast<std::uint64_t>(modifiedAt(status).tv_nsec)};
}

/// Bytes read, 0 at the end, negative on failure.
std::int64_t readSome(const Handle& file, std::byte* into, std::size_t most) {
    for (;;) {
        const ssize_t kRead = ::read(file.get(), into, most);
        if (kRead < 0 && errno == EINTR) {
            continue;
        }
        return kRead;
    }
}

} // namespace

struct DirectorySource::Root {
    Handle directory;
    dev_t device = 0;
};

result::Result<std::shared_ptr<const DirectorySource>> DirectorySource::open(const std::filesystem::path& root,
                                                                             std::string_view unavailable) {
    auto held = std::make_unique<Root>();
    held->directory = Handle{::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    struct stat status{};
    if (!held->directory.valid() || ::fstat(held->directory.get(), &status) != 0) {
        return refuse(ContentError::SourceUnavailable, result::ErrorClass::Unavailable, unavailable);
    }
    held->device = status.st_dev;
    return std::make_shared<const DirectorySource>(std::move(held));
}

namespace {

/// Refuses what was opened on the way if it is a link or on another
/// filesystem.
result::Status checkOpened(const Handle& opened, dev_t device, std::string_view what) {
    if (!opened.valid()) {
        if (errno == ELOOP || errno == ENOTDIR) {
            return linkOnTheWay();
        }
        return cannotOpen(what);
    }
    struct stat status{};
    if (::fstat(opened.get(), &status) != 0 || status.st_dev != device) {
        return anotherFilesystem();
    }
    return {};
}

/// The file at a locator, segment by segment from the root: no link is
/// followed and no other filesystem entered, so nothing outside the root is
/// reachable.
result::Result<Handle> openBeneath(const DirectorySource::Root& root, std::string_view locator) {
    Handle directory;
    int at = root.directory.get();
    std::size_t start = 0;
    for (std::size_t end = locator.find('/'); end != std::string_view::npos; end = locator.find('/', start)) {
        const std::string kSegment{locator.substr(start, end - start)};
        Handle next{::openat(at, kSegment.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        RAWFRAME_TRY(checkOpened(next, root.device, "a directory on the way"));
        directory = std::move(next);
        at = directory.get();
        start = end + 1;
    }
    const std::string kName{locator.substr(start)};
    Handle file{::openat(at, kName.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
    RAWFRAME_TRY(checkOpened(file, root.device, "the file"));
    return file;
}

#endif

/// The regular file at a locator and what it is when opened.
result::Result<std::pair<Handle, Status>> openRegular(const DirectorySource::Root& root, std::string_view locator) {
    RAWFRAME_TRY_ASSIGN(Handle file, openBeneath(root, locator));
    const auto kStatus = statusOf(file);
    if (!kStatus || !kStatus->regular) {
        return refuse(ContentError::ReadFailed, result::ErrorClass::Unavailable, "not a regular file");
    }
    return std::pair{std::move(file), *kStatus};
}

} // namespace

DirectorySource::DirectorySource(std::unique_ptr<Root> root) noexcept : root_(std::move(root)) {
}

DirectorySource::~DirectorySource() = default;

result::Result<std::vector<std::byte>> DirectorySource::read(std::string_view locator, std::uint64_t length) const {
    RAWFRAME_TRY_ASSIGN(const auto kOpened, openRegular(*root_, locator));
    const auto& [file, before] = kOpened;
    if (before.size < length) {
        return refuse(ContentError::ShortRead, result::ErrorClass::DataLoss, "the file is shorter than declared");
    }
    if (before.size > length) {
        return refuse(ContentError::SourceChanged, result::ErrorClass::DataLoss, "the file is longer than declared");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    std::size_t done = 0;
    while (done < bytes.size()) {
        const std::int64_t kRead = readSome(file, bytes.data() + done, bytes.size() - done);
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
    if (readSome(file, &extra, 1) != 0 || statusOf(file) != before) {
        return refuse(ContentError::SourceChanged, result::ErrorClass::DataLoss, "the file changed while it was read");
    }
    return bytes;
}

result::Result<std::uint64_t> DirectorySource::size(std::string_view locator) const {
    RAWFRAME_TRY_ASSIGN(const auto kOpened, openRegular(*root_, locator));
    return kOpened.second.size;
}

} // namespace rawframe::content

#endif
