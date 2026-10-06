#pragma once

// A client of a running Runtime's tooling endpoint (world_tooling/server.h):
// `rawframe-author connect` (D408), and a session's preview (D433).

#include "rawframe/network/provider.h"
#include "rawframe/network_quic/quic.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::author {

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
    class Client;
    ToolingLink();

    std::unique_ptr<network_quic::QuicNetwork> network_;
    std::unique_ptr<network::Provider> provider_;
    std::unique_ptr<Client> client_;
};

/// `rawframe-author connect`: opens a link, writes the welcome, then sends
/// each line of standard input as a record and writes each reply, a line
/// for a line, until the input ends (when it says `tooling.end`) or the
/// endpoint closes. 0 when every reply was an answer.
int connect(const char* endpoint, const char* pinFile, const char* tokenFile);

/// `endpoint` (`host:port`) written again from what it was read as, where
/// its host is a loopback literal: 127.0.0.0/8 in four plain decimal parts,
/// or `[::1]`. None for anything else, a name such as `localhost` among
/// them, since a name is resolved and may lead off this machine.
std::optional<std::string> loopbackEndpoint(std::string_view endpoint);

} // namespace rawframe::author
