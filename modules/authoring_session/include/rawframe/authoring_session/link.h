#pragma once

// A client of a running Runtime's tooling endpoint (world_tooling/server.h):
// what `rawframe-author connect` speaks through (D408), and a session's
// preview (D433); and a collaborator's of a shared session's host
// (host.h), what `rawframe-author join` speaks through (D881).

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/document/json.h"
#include "rawframe/network/provider.h"
#include "rawframe/network_quic/quic.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

class LineClient;
class Preview;

/// One connection to a tooling endpoint, welcomed; said `tooling.end` and
/// closed when it goes.
class ToolingLink {
public:
    ToolingLink(const ToolingLink&) = delete;
    ToolingLink& operator=(const ToolingLink&) = delete;
    ~ToolingLink();

    /// Connects to `endpoint`, trusting only the certificate `pinFile`
    /// names, and says hello with the token in `tokenFile`, each the first
    /// line of a regular file of at most 4 KiB. `said` is the
    /// welcome line, or the endpoint's refusal line, or why it could not be
    /// reached; the link only when welcomed.
    static std::unique_ptr<ToolingLink>
    open(std::string_view endpoint, const char* pinFile, const char* tokenFile, std::string& said);

    /// Sends one record and waits for its reply line; none when the
    /// endpoint closed or kept silent.
    std::optional<std::string> ask(std::string_view record);
    /// The same, a last record before the link is let go, waiting only a
    /// moment: a program stopped by the system never replies, and the
    /// ten seconds an ask waits held Studio's end (D947).
    std::optional<std::string> askLast(std::string_view record);
    /// Sends one record answered later (D969): its reply kept for `heard`
    /// under the record's own id, so who sends it never waits on the
    /// endpoint's next frame. Whether it was sent.
    bool send(std::string_view record);
    /// The replies to records sent since last asked, each under its own
    /// id, and what the endpoint said unasked; without waiting.
    std::vector<std::string> heard();

private:
    ToolingLink();

    std::unique_ptr<network_quic::QuicNetwork> network_;
    std::unique_ptr<network::Provider> provider_;
    std::unique_ptr<LineClient> client_;
};

/// One collaborator's connection to a shared session's host, welcomed;
/// said `authoring.end` and closed when it goes.
class HostLink {
public:
    HostLink(const HostLink&) = delete;
    HostLink& operator=(const HostLink&) = delete;
    ~HostLink();

    /// Connects to `endpoint`, trusting only the certificate `pinFile`
    /// names, and says hello with the token in `tokenFile`, as
    /// `ToolingLink::open` does; `said` is the welcome, or the host's
    /// refusal, or why it could not be reached.
    static std::unique_ptr<HostLink>
    open(std::string_view endpoint, const char* pinFile, const char* tokenFile, std::string& said);

    /// Sends one record and waits for its reply line; none when the host
    /// closed or kept silent. What the host says unasked meanwhile is kept
    /// for `heard`.
    std::optional<std::string> ask(std::string_view record);
    /// Sends one record the host answers when it ends (`cook`, D885),
    /// its progress and reply then heard under its own id; whether it was
    /// sent. `cancel`'s operation is that id too.
    [[nodiscard]] bool send(std::string_view record);
    /// The number a record sent with `send` went out under, by its own id.
    [[nodiscard]] std::optional<std::int64_t> sentAs(const document::Value& id) const;
    /// What the host said unasked (`authoring.changed`, presence, a
    /// revocation, and what a record sent is answered with), waiting up to
    /// `withinMilliseconds` for the first where none was kept.
    std::vector<std::string> heard(std::int64_t withinMilliseconds);
    /// Whether the host closed the connection.
    [[nodiscard]] bool closed() const noexcept;

private:
    HostLink();

    std::unique_ptr<network_quic::QuicNetwork> network_;
    std::unique_ptr<network::Provider> provider_;
    std::unique_ptr<LineClient> client_;
};

/// A shared session's host joined, answering records as a session held
/// here does (D883), so a client of one speaks to the other unchanged: its
/// hello answered with the host's welcome, what the host says unasked
/// given by `poll`, and `end` ending the connection. A record the host
/// answers when it ends (`cook`) is answered as a session answers it,
/// later, through `poll` (D885). The preview of a game played here, which
/// the host cannot reach, is held here (D906): `preview` and the records
/// that only act on it are answered here, the view handed it where this
/// author looks, as they say it and as the host answers.
class JoinedSession final : public Answering {
public:
    JoinedSession(const JoinedSession&) = delete;
    JoinedSession& operator=(const JoinedSession&) = delete;
    ~JoinedSession() override;
    /// Joins as `HostLink::open` does; `said` is the welcome, or why not.
    static std::unique_ptr<JoinedSession>
    open(std::string_view endpoint, const char* pinFile, const char* tokenFile, std::string& said);

    [[nodiscard]] std::string answer(std::string_view line, bool& ended) override;
    [[nodiscard]] std::vector<std::string> poll() override;
    /// Whether the host closed the connection.
    [[nodiscard]] bool closed() const noexcept;

private:
    JoinedSession(std::unique_ptr<HostLink> link, std::string welcome);
    /// A record for the preview held here, answered here.
    [[nodiscard]] std::string answerHere(std::string_view line);
    /// Where this author looks in `scene`, as a record or a reply says it,
    /// handed the preview where it shows that scene.
    void looked(const document::Value& said, std::string_view scene);

    std::unique_ptr<HostLink> link_;
    std::string welcome_;
    std::unique_ptr<Preview> preview_;
    std::map<std::string, authoring::SceneView, std::less<>> views_;
};

/// Whether a reply line is an answer rather than an error.
bool answered(std::string_view reply);

/// `endpoint` (`host:port`) written again from what it was read as, where
/// its host is a loopback literal: 127.0.0.0/8 in four plain decimal parts,
/// or `[::1]`. None for anything else, a name such as `localhost` among
/// them, since a name is resolved and may lead off this machine.
std::optional<std::string> loopbackEndpoint(std::string_view endpoint);

} // namespace rawframe::authoring_session
