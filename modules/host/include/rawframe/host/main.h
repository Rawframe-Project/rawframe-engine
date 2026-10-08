#pragma once

// What every process entry does around one Host (ADR-0017): read
// `--config <file>`, whose relative paths are under the file's own directory
// (D188), bridge SIGINT and SIGTERM (console control events on Windows, D237)
// to an orderly stop, write NDJSON diagnostics to standard output, and return
// the Host's exit code (D184).
// Launch failures happen before diagnostics exist, so they go to standard
// error as plain text and exit with 64, or 65 for a configuration that does
// not parse. A process that sleeps between
// iterations and reads its configuration from a file: only where there are
// both threads and files; a browser drives a Host itself (D168).

#include "rawframe/base/platform.h"
#include "rawframe/composition/declaration.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/host/host.h"

#include <atomic>
#include <span>
#include <string_view>

#if RAWFRAME_THREADS && RAWFRAME_FILE_SYSTEM
namespace rawframe::host {

/// Runs the Host the entry describes and says how it ended. `runHost` by
/// default; a client whose window system owns the loop drives the Host from
/// its frames instead.
using HostDriver = HostExit (*)(const HostRequest& request) noexcept;

struct ProcessEntry {
    /// The executable's name, for launch errors.
    std::string_view name;
    composition::TargetRole role = composition::TargetRole::DedicatedServer;
    std::span<const composition::RegistrarEntry> registrars;
    HostDriver drive = nullptr;
    /// The program's shutdown budget when its configuration names none.
    std::uint32_t defaultShutdownBudgetMs = 5000;
};

[[nodiscard]] int hostMain(int argc, char** argv, const ProcessEntry& entry);

/// Bridges this process's stop requests to `stopRequested`: what `hostMain`
/// does, and a tool that stops in order without a Host does too (the cook,
/// D502). Installing it again changes nothing.
void installStopBridge();

/// Whether a stop has been requested of this process since its bridge was
/// installed: the one process-wide value in the engine, only ever set.
[[nodiscard]] const std::atomic<bool>& stopRequested() noexcept;

} // namespace rawframe::host
#endif
