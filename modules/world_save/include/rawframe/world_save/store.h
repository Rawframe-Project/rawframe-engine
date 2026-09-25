#pragma once

// Where saves are kept (ADR-0057's provider boundary): a store keeps a
// save's bytes under a slot name and gives them back, and knows nothing of
// what they hold. Absence and failure are typed; a store never truncates,
// and a save being kept never destroys the one kept before it until it is
// whole. Calls block, so they belong on a blocking-I/O worker or the Host
// thread, never a CPU worker.

#include "rawframe/base/platform.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#endif

namespace rawframe::world_save {

class SaveStore {
public:
    SaveStore() = default;
    SaveStore(const SaveStore&) = delete;
    SaveStore& operator=(const SaveStore&) = delete;
    virtual ~SaveStore() = default;

    /// The bytes kept under `slot`: `absent` if none are, `limit_exceeded`
    /// past `maximumBytes`.
    [[nodiscard]] virtual result::Result<std::vector<std::byte>> load(std::string_view slot,
                                                                      std::size_t maximumBytes) = 0;
    /// Keeps `bytes` under `slot`, replacing what was there only once they
    /// are whole.
    [[nodiscard]] virtual result::Status keep(std::string_view slot, std::span<const std::byte> bytes) = 0;
};

#if RAWFRAME_FILE_SYSTEM
/// The first-party store: one file per slot in a directory the operator
/// names, `<slot>.rfsave`. A save is written beside its file, flushed to
/// the disk, and renamed over it, so a crash leaves the old save or the new
/// one, never part of either.
class DirectorySaveStore final : public SaveStore {
public:
    explicit DirectorySaveStore(std::filesystem::path directory) noexcept : directory_(std::move(directory)) {
    }

    [[nodiscard]] result::Result<std::vector<std::byte>> load(std::string_view slot, std::size_t maximumBytes) override;
    [[nodiscard]] result::Status keep(std::string_view slot, std::span<const std::byte> bytes) override;

private:
    std::filesystem::path directory_;
};
#endif

} // namespace rawframe::world_save
