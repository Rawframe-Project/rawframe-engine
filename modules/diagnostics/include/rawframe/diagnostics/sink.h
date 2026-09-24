#pragma once

#include "rawframe/diagnostics/record.h"

namespace rawframe::diagnostics {

/// A destination for records. `accept` is called on the emitting thread, may be
/// called from many threads at once, and must neither block on I/O nor fail:
/// a sink serializes into memory it owns and writes later, when its owner
/// drains it (SPEC-0047, the write path). A sink never logs through a router.
class Sink {
public:
    Sink() = default;
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
    virtual ~Sink() = default;

    virtual void accept(const Record& record) noexcept = 0;
};

} // namespace rawframe::diagnostics
