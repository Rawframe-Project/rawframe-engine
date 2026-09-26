#pragma once

// The Host's two executors for tests that start a Composition, as a Host
// hands them to its participants: a participant that declares one, such as
// checkpoints sealing and writing off the World's safe point (D227), is
// admitted only with it. One worker each, made on first use and kept for
// the test process. Header-only: include it from a test that links the
// execution module.

#include "rawframe/execution/executor.h"

namespace rawframe::test {

inline execution::Executor& cpuExecutor() {
    static execution::Executor executor{{.kind = execution::ExecutorKind::Cpu, .workers = 1}};
    return executor;
}

inline execution::Executor& blockingIoExecutor() {
    static execution::Executor executor{{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    return executor;
}

} // namespace rawframe::test
