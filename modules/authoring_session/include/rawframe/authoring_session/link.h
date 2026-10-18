#pragma once

// A client of a running Runtime's tooling endpoint (world_tooling/server.h):
// what `rawframe-author connect` speaks through (D408), and a session's
// preview (D433); and a collaborator's of a shared session's host
// (host.h), what `rawframe-author join` speaks through (D881).

#include "rawframe/network/provider.h"
#include "rawframe/network_quic/quic.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

class LineClient;

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
    /// What the host said unasked (`authoring.changed`, presence, and a
    /// revocation's refusal), waiting up to `withinMilliseconds` for the
    /// first where none was kept.
    std::vector<std::string> heard(std::int64_t withinMilliseconds);
    /// Whether the host closed the connection.
    [[nodiscard]] bool closed() const noexcept;

private:
    HostLink();

    std::unique_ptr<network_quic::QuicNetwork> network_;
    std::unique_ptr<network::Provider> provider_;
    std::unique_ptr<LineClient> client_;
};

/// Whether a reply line is an answer rather than an error.
bool answered(std::string_view reply);

/// `endpoint` (`host:port`) written again from what it was read as, where
/// its host is a loopback literal: 127.0.0.0/8 in four plain decimal parts,
/// or `[::1]`. None for anything else, a name such as `localhost` among
/// them, since a name is resolved and may lead off this machine.
std::optional<std::string> loopbackEndpoint(std::string_view endpoint);

} // namespace rawframe::authoring_session
