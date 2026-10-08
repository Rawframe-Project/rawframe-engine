#pragma once

// An authoring session (D407, authoring/session.h): a game read once, its
// scenes under a root opened as records name them and kept open with their
// undo histories, each change written to its file as it commits, each
// with its selection (D417) and its view (D432), and one preview a running
// client's tooling endpoint shows (D433). `rawframe-author session` holds
// one on standard input and output; any other client may hold one in its
// own process, speaking exactly the records every client of the session
// speaks.

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

class Session {
public:
    /// The game described at `game`, its scenes under `root`; read at
    /// `hello`. `cook` is the cook tool `authoring.cook` runs (D502); none
    /// refuses it.
    Session(std::filesystem::path game, std::filesystem::path root, std::filesystem::path cook = {});
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    /// Gives a preview's player its camera back.
    ~Session();

    /// One record, a line without its line feed, answered: the reply line,
    /// its line feed included, or nothing for an operation that answers
    /// when it ends (`cook`, D502). `ended` is set by `end`, whose reply
    /// follows the last records of an operation it stopped.
    [[nodiscard]] std::string answer(std::string_view line, bool& ended);

    /// What the session says unasked, each a line with its line feed: a
    /// running operation's progress, and its reply once it ends. Asked as
    /// often as a client wants to hear it.
    [[nodiscard]] std::vector<std::string> poll();
    /// Whether every record so far succeeded, each slot of its answer too.
    [[nodiscard]] bool clean() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::authoring_session
