#pragma once

// The Kest project beside a description the cook reads, shared by the game
// and mod importers: a program they name compiles from its sources.

#include "rawframe/base/bits128.h"
#include "rawframe/cook/cook.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"

#include <string_view>
#include <vector>

namespace rawframe::cook {

/// The Kest sources of the `kest.project` beside the description, named by
/// its sidecar, and their files.
struct KestProject {
    base::Bits128 sources{};
    std::vector<kest::SourceFile> files;
};

/// Reads the project beside the description; refused (`BadReference`,
/// naming `program`) without a sidecar that names rawframe.kest.
[[nodiscard]] result::Result<KestProject> projectBeside(Reads& reads, std::string_view program);

/// Refused (`BadReference`) unless `program` is a plain path under the
/// description's directory that compiles, with the engine's library, from
/// `project`'s files; the report is on the error.
[[nodiscard]] result::Status compiles(const KestProject& project, std::string_view program);

} // namespace rawframe::cook
