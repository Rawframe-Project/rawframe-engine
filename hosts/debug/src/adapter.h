#pragma once

// The Debug Adapter Protocol spoken for a running game (ADR-0066's debugger
// bridge, D461): an editor's requests turned into the tooling endpoint's
// debug verbs (D460), and what the game does turned into the protocol's
// events. Function breakpoints, the stopped frames and their arguments, and
// carrying on; line breakpoints and stepping are answered as not yet
// offered, until Kest's public header says where code was written.

#include "rawframe/authoring_session/attach.h"
#include "rawframe/authoring_session/link.h"
#include "rawframe/document/json.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::debug_adapter {

class Adapter {
public:
    /// The messages to send for one request: its response, and any events.
    [[nodiscard]] std::vector<document::Value> handle(const document::Value& request);
    /// Asked a few times a second: a `stopped` event when the game stopped
    /// since last told, `terminated` when the endpoint went.
    [[nodiscard]] std::vector<document::Value> poll();
    /// Whether the editor said to disconnect, or the game went.
    [[nodiscard]] bool done() const noexcept {
        return done_;
    }

private:
    document::Value
    response(const document::Value& request, bool success, document::Value body = {}, const std::string& message = {});
    document::Value event(std::string name, document::Value body);
    /// A debug verb asked of the game: its answer, or none (the reason in
    /// `why`).
    std::optional<document::Value> ask(document::Value record, std::string& why);

    std::unique_ptr<authoring_session::ToolingLink> link_;
    std::int64_t seq_ = 0;
    std::int64_t asked_ = 0;
    /// The frames last read while stopped, and whether the editor was told
    /// of the stop.
    std::vector<document::Value> frames_;
    bool told_ = false;
    bool done_ = false;
    bool terminated_ = false;
};

} // namespace rawframe::debug_adapter
