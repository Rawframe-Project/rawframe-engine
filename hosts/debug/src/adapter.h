#pragma once

// The Debug Adapter Protocol spoken for a running game (ADR-0066's debugger
// bridge, D461): an editor's requests turned into the tooling endpoint's
// debug verbs (D460), and what the game does turned into the protocol's
// events. Function and line breakpoints (D630), the stopped frames, where
// they stand, and their locals (D631), and carrying on; stepping is
// answered as not yet offered.

#include "rawframe/authoring_session/attach.h"
#include "rawframe/authoring_session/link.h"
#include "rawframe/document/json.h"

#include <cstdint>
#include <filesystem>
#include <map>
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
    /// Every breakpoint asked for, the functions and each source's lines,
    /// written into the game at once (D630): its answer, or none.
    std::optional<document::Value> breakAll(std::string& why);
    /// An editor's path as the game's build names its file: under the
    /// game's directory, the path from it; else the file's name alone,
    /// which the game matches as the end of its own.
    [[nodiscard]] std::string builtName(const std::string& path) const;
    /// A frame's file as the editor opens it: under the game's directory
    /// where it is known; none where it is not.
    [[nodiscard]] std::optional<std::filesystem::path> editorPath(const std::string& built) const;

    std::unique_ptr<authoring_session::ToolingLink> link_;
    std::int64_t seq_ = 0;
    std::int64_t asked_ = 0;
    /// The frames last read while stopped, and whether the editor was told
    /// of the stop.
    std::vector<document::Value> frames_;
    /// The game's directory where it was attached by the game, and the
    /// breakpoints asked for: functions, and lines by the build's file.
    std::filesystem::path gameDirectory_;
    std::vector<std::string> functions_;
    std::map<std::string, std::vector<std::int64_t>> lines_;
    bool told_ = false;
    bool done_ = false;
    bool terminated_ = false;
};

} // namespace rawframe::debug_adapter
