#pragma once

// What every process entry does around one Host (ADR-0017): read
// `--config <file>`, bridge SIGINT and SIGTERM to an orderly stop, write
// NDJSON diagnostics to standard output, and return the Host's exit code.
// Launch failures happen before diagnostics exist, so they go to standard
// error as plain text and exit with 64. A process that sleeps between
// iterations and reads its configuration from a file: only where there are
// both threads and files; a browser drives a Host itself (D168).

#include "rawframe/base/platform.h"
#include "rawframe/composition/declaration.h"
#include "rawframe/composition/registrar.h"

#include <span>
#include <string_view>

#if RAWFRAME_THREADS && RAWFRAME_FILE_SYSTEM
namespace rawframe::host {

struct ProcessEntry {
    /// The executable's name, for launch errors.
    std::string_view name;
    composition::TargetRole role = composition::TargetRole::DedicatedServer;
    std::span<const composition::RegistrarEntry> registrars;
};

[[nodiscard]] int hostMain(int argc, char** argv, const ProcessEntry& entry);

} // namespace rawframe::host
#endif
