#pragma once

// The one place Maul Window's types live: the running context, and the
// conversions between its records and the engine's. Nothing outside this
// module's sources includes it.

#include "rawframe/result/result.h"
#include "rawframe/window/events.h"
#include "rawframe/window/windows.h"

#include <maul-window/context.h>
#include <maul-window/event.h>
#include <optional>
#include <string_view>

namespace rawframe::window {

struct Platform {
    explicit Platform(Program& running) noexcept : program(&running) {
    }

    Program* program;
    mwinContext* context = nullptr;
    Windows windows{*this};
    /// What start said, for stop and for run's own result.
    result::Status started;
    /// Run returned while the page's frames run the program on (the web):
    /// stop frees this Platform.
    bool outlivesRun = false;

    /// The context behind a Windows, for the test platform's calls.
    [[nodiscard]] static mwinContext* contextOf(Windows& windows) noexcept {
        return windows.platform_->context;
    }

    /// Runs `program` on the backend: Maul Window's loop, its three calls
    /// forwarded to the program.
    [[nodiscard]] static result::Status run(Program& program, const RunSettings& settings, mwinBackendKind backend);
};

/// The Error for a Maul Window failure; `what` names the call.
[[nodiscard]] std::unexpected<result::Error> failure(mwinResult status, std::string_view what);

[[nodiscard]] inline mwinWindowId toMaul(WindowId id) noexcept {
    return {.index1 = id.index, .generation = id.generation};
}
[[nodiscard]] inline WindowId fromMaul(mwinWindowId id) noexcept {
    return {.index = id.index1, .generation = id.generation};
}
[[nodiscard]] inline RequestId fromMaul(mwinRequestId id) noexcept {
    return {.index = id.index1, .generation = id.generation};
}
[[nodiscard]] inline GamepadId fromMaul(mwinGamepadId id) noexcept {
    return {.index = id.index1, .generation = id.generation};
}
[[nodiscard]] inline mwinGamepadId toMaul(GamepadId id) noexcept {
    return {.index1 = id.index, .generation = id.generation};
}

[[nodiscard]] mwinWindowMode toMaul(Mode mode) noexcept;
[[nodiscard]] mwinRequestKind toMaul(RequestKind kind) noexcept;
[[nodiscard]] mwinOutcome toMaul(Outcome outcome) noexcept;

/// The engine's record for one of Maul Window's, with its text and a
/// drop's paths copied out; nothing for a record past SPEC-0025's
/// vocabularies (D248), which the stream skips.
[[nodiscard]] std::optional<Event> fromMaul(const mwinContext& context, const mwinEvent& record);

/// Maul Window's record for a report the test platform takes; Invalid for
/// a kind it takes elsewhere or never. Its text points into `event`.
[[nodiscard]] result::Result<mwinEvent> toMaul(const Event& event);

} // namespace rawframe::window
