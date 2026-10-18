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

/// What answers a session's records, a line each: a session held in this
/// process, or a shared session's host joined (D883, link.h), which a
/// client such as the MCP bridge speaks to the same way.
class Answering {
public:
    virtual ~Answering() = default;
    /// One record, a line without its line feed, answered: the reply line,
    /// its line feed included, or nothing for one answered when it ends.
    /// `ended` is set by `end`.
    [[nodiscard]] virtual std::string answer(std::string_view line, bool& ended) = 0;
    /// What is said unasked, each a line with its line feed.
    [[nodiscard]] virtual std::vector<std::string> poll() = 0;

protected:
    Answering() = default;
    Answering(const Answering&) = default;
    Answering& operator=(const Answering&) = default;
};

class Session final : public Answering {
public:
    /// The game described at `game`, its scenes under `root`; read at
    /// `hello`. `cook` is the cook tool `authoring.cook` runs (D502); none
    /// refuses it.
    Session(std::filesystem::path game, std::filesystem::path root, std::filesystem::path cook = {});
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    /// Gives a preview's player its camera back.
    ~Session() override;

    /// One record, a line without its line feed, answered: the reply line,
    /// its line feed included, or nothing for an operation that answers
    /// when it ends (`cook`, D502). `ended` is set by `end`, whose reply
    /// follows the last records of an operation it stopped.
    [[nodiscard]] std::string answer(std::string_view line, bool& ended) override;

    /// What the session says unasked, each a line with its line feed: a
    /// running operation's progress, and its reply once it ends. Asked as
    /// often as a client wants to hear it.
    [[nodiscard]] std::vector<std::string> poll() override;
    /// Whether every record so far succeeded, each slot of its answer too.
    [[nodiscard]] bool clean() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::authoring_session
