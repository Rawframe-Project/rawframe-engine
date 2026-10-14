#pragma once

// Studio's Console (D778, SPEC-0051 Part 5): what Studio said and what the
// game it plays logs, read from the NDJSON files its server and client
// write as they grow. Presentation only: the logs are the headless path.

#include "records.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::studio {

/// One line of the Console: where it came from, how severe it is, what it
/// says, and the place in the game's own files it names, if any.
struct ConsoleLine {
    enum class Source : std::uint8_t {
        Studio,
        Server,
        Client
    };
    enum class Level : std::uint8_t {
        Info,
        Warning,
        Error
    };
    Source source = Source::Studio;
    Level level = Level::Info;
    std::string text;
    std::optional<Diagnostic> at;
};

/// A log record's Console line, or none for a line that is not a log
/// record.
[[nodiscard]] std::optional<ConsoleLine> consoleLineOf(std::string_view record, ConsoleLine::Source source);

/// The first place in the game's own files `text` names, as
/// `game/<path>:<line>:<column>`, if it names one (D453: only those are the
/// author's to open).
[[nodiscard]] std::optional<Diagnostic> gamePlaceIn(std::string_view text);

/// A log file read as it grows, a bounded piece at a time; a record split
/// across reads waits for its end.
class LogTail {
public:
    LogTail(std::filesystem::path file, ConsoleLine::Source source);

    /// The records written since the last read, from at most `most` bytes.
    [[nodiscard]] std::vector<ConsoleLine> read(std::size_t most = 256 * 1024);

private:
    std::filesystem::path file_;
    ConsoleLine::Source source_;
    std::uintmax_t offset_ = 0;
    std::string partial_;
};

} // namespace rawframe::studio
