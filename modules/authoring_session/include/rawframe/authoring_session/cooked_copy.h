#pragma once

// What a cook wrote, brought from the session that cooked it into a
// directory here: so a collaborator plays what a shared session's
// host cooked on its own machine, through records alone, as Studio joined
// and `rawframe-author copy-cooked` do.

#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace rawframe::authoring_session {

/// What a copy brought: the files read over, those `into` already held as
/// the manifest gives them, and the bytes read.
struct CopiedCook {
    std::size_t copied = 0;
    std::size_t kept = 0;
    std::uint64_t bytes = 0;
};

/// One record asked of a session, without its id, which the asker gives
/// it; its reply line.
using AskSession = std::function<std::string(document::Value record)>;

/// The cook into `output` (a path where the session is held here, the plain
/// name it was cooked under at a host) brought into `into` through `ask`'s
/// `authoring.cooked_file` records: its manifest read, then each file it
/// locates that `into` does not hold at the manifest's length and digest,
/// a piece at a time, checked against them and put in place whole; the
/// manifest is put last, so `into` holds the cook it held or this one.
/// Refused with the session's own words where it refuses a record, and for
/// a manifest or a file that does not hold together.
[[nodiscard]] result::Result<CopiedCook>
copyCooked(const AskSession& ask, std::string_view output, const std::filesystem::path& into);

} // namespace rawframe::authoring_session
