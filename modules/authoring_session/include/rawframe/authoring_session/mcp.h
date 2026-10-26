#pragma once

// An authoring session offered to AI agents over the Model Context Protocol:
// JSON-RPC messages, a line each, as MCP's standard input and output
// transport carries them. Every operation `authoring.describe` lists is a
// tool, named for it, and so are the session's own verbs an agent needs
// (the game's catalog and assets, undo and redo, history, a new scene, a
// batch of operations, cooking, importing). The bridge only translates:
// each call becomes one of the session's records and its reply the call's
// result, so an agent gains no operation a client of the records lacks and
// loses none (a game is made without Studio). The tools
// are read from the session's own discovery, never listed a second time.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

class Answering;

class McpBridge {
public:
    /// Greets `session` and reads its discovery; the session must not have
    /// been greeted, and outlives the bridge. It is one held here, or a
    /// shared session's host joined.
    explicit McpBridge(Answering& session);
    McpBridge(const McpBridge&) = delete;
    McpBridge& operator=(const McpBridge&) = delete;
    ~McpBridge();

    /// One JSON-RPC message, a line without its line feed, answered: the
    /// reply line with its line feed, or nothing for a notification and for
    /// a call whose operation answers when it ends (cooking).
    [[nodiscard]] std::string answer(std::string_view line);

    /// What the bridge says unasked, each a line with its line feed: a
    /// running operation's progress, to a caller that asked for it, and
    /// the reply to a call whose operation has ended.
    [[nodiscard]] std::vector<std::string> poll();

    /// Ends the session, as standard input closing does: a running
    /// operation is stopped, and the lines its call is answered with.
    [[nodiscard]] std::vector<std::string> end();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::authoring_session
