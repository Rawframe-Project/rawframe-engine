#pragma once

// A session's long-running operation (SPEC-0040, D502): the game's
// directory cooked by the cook tool, run as a child process so a source
// that brings an importer down brings down only the cook. What it says as
// it goes becomes progress records; how it ends, the operation's outcome:
// cooked, cancelled, or the error record.

#include "rawframe/document/json.h"
#include "rawframe/process/child.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::authoring_session {

/// SPEC-0040's limit point for a session's asynchronous operations in
/// flight: one cook at a time.
inline constexpr std::size_t kMaximumSessionOperations = 1;

class Cooking {
public:
    /// Cooks with `program` (rawframe-cook); none refuses every cook.
    explicit Cooking(std::filesystem::path program);
    Cooking(const Cooking&) = delete;
    Cooking& operator=(const Cooking&) = delete;
    /// Kills a cook still running; `end` first stops it in order.
    ~Cooking();

    /// Starts cooking `sources` into `output`, reusing `cache` if given;
    /// the operation is known by `id`, its client's. Refuses a cook while
    /// one runs (`limit_exceeded`), a session with no cook tool
    /// (`unsupported_operation`), and an output it cannot make
    /// (`validation_failed`). What the tool itself refuses, as an output
    /// inside the sources, is the operation's error outcome.
    [[nodiscard]] result::Status start(const document::Value& id,
                                       const std::filesystem::path& sources,
                                       const std::filesystem::path& output,
                                       const std::optional<std::filesystem::path>& cache);

    /// Asks the cook known by `id` to stop; false when none runs by it.
    bool cancel(const document::Value& id);

    /// The records said since last asked, each a line with its line feed:
    /// progress as the cook goes, then its outcome once it has ended.
    /// `failed` is set when that outcome is an error.
    [[nodiscard]] std::vector<std::string> poll(bool& failed);

    /// Stops a running cook in order, waiting a few seconds before killing
    /// it, and gives its last records, the outcome cancelled as the session
    /// ended.
    [[nodiscard]] std::vector<std::string> end();

    [[nodiscard]] bool running() const noexcept {
        return child_.has_value();
    }

private:
    std::filesystem::path program_;
    std::optional<process::Child> child_;
    document::Value id_;
    std::filesystem::path log_;
    std::uintmax_t read_ = 0;
    std::string partial_;
    /// Why it was asked to stop, if it was: `requested` or `ended`.
    std::string stopping_;
};

} // namespace rawframe::authoring_session
