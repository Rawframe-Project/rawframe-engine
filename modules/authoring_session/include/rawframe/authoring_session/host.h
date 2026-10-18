#pragma once

// A shared authoring session (ADR-0085 sections 2 and 6, D881): one
// session several collaborators speak to over QUIC, on a provider whose
// application protocol is `rawframe-authoring-v1`, the host ordering every
// record as it arrives. A collaborator opens one bidirectional stream and
// speaks the session's own records on it, a line each (authoring/session.h),
// the first a hello naming a token the host's owner issued:
//
//   {"kind":"authoring.hello","id":1,"surfaceGeneration":1,"token":"..."}
//   {"kind":"authoring.reply","id":1,"answer":{"kind":"authoring.welcome",
//    "surfaceGeneration":1,"author":"ada","grants":["read","edit"]}}
//
// A token holds a name and grants, each of a kind of record: `read` (what
// reads: describe, read, history, assets, presence, and the like, and
// saying one's own presence), `edit` (what changes the documents or their
// editor state), `cook` (cook, cancel, import), `play` (the preview and the
// played game), and `claim` (claim, release). A record its token does not
// grant is refused. A collaborator's cook names its output and cache by
// plain names, which the host puts under its cooking directory, in the
// collaborator's own (D885). A record is the token holder's: its author is the
// token's name, set by the host, and one naming another is refused, so no
// collaborator acts as another. `end` ends the collaborator's connection,
// never the session, and a connection's end lets go of its presence and
// claims.
//
// After a record changes a document, every other collaborator is told, so
// each reads what it shows again from the host, the documents' only
// holder:
//
//   {"kind":"authoring.changed","author":"ada","document":"level.scene","digest":"sha256:..."}
//
// and when presence or claims change, everyone is told who is present, as
// `authoring.presence` answers it, at most ten times a second. The tokens
// file is read again when it changes; a collaborator whose token it no
// longer holds is told so and disconnected at once:
//
//   {"kind":"authoring.revoked","author":"ben"}

#include "rawframe/authoring_session/session.h"
#include "rawframe/execution/time.h"
#include "rawframe/network/provider.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

/// What a token lets its holder do.
struct Grants {
    bool read = false;
    bool edit = false;
    bool cook = false;
    bool play = false;
    bool claim = false;
};

/// A token the host's owner issued: the secret, the name its holder
/// authors under, and their grants.
struct Collaborator {
    std::string token;
    std::string name;
    Grants grants;
};

/// The most collaborators a tokens file names.
inline constexpr std::size_t kMostCollaborators = 256;

/// A tokens file: a line each, `<token> <name> <grant>...`, a token of 32
/// to 256 printable bytes, a plain name (1 to 64 letters, digits, `.`,
/// `_`, and `-`, not starting with `.`, D885), and one or more of the
/// grants above; blank lines and lines starting `#` pass.
/// Refuses (`ValidationFailed`, naming the line) anything else, a token or
/// a name given twice, and more than `kMostCollaborators`.
[[nodiscard]] result::Result<std::vector<Collaborator>> readCollaborators(std::string_view text);

/// The provider bounds a host asks for, `collaborators` at once.
[[nodiscard]] network::ProviderProfile authoringProfile(std::size_t collaborators) noexcept;

class AuthoringHost {
public:
    /// A host of `session`, which has not been said hello to, on
    /// `provider`, which listens already or will, its collaborators those
    /// the file at `tokens` names; a collaborator's cook goes into
    /// `cooks`, and none is refused where it names none. Refuses a tokens
    /// file that does not read, and a session that does not greet it.
    [[nodiscard]] static result::Result<std::unique_ptr<AuthoringHost>>
    create(network::Provider& provider,
           Session& session,
           std::filesystem::path tokens,
           std::optional<std::filesystem::path> cooks = std::nullopt);

    AuthoringHost(const AuthoringHost&) = delete;
    AuthoringHost& operator=(const AuthoringHost&) = delete;
    ~AuthoringHost();

    /// Takes what arrived and answers each collaborator's records in the
    /// order they came, tells the others what changed, says what the
    /// session says unasked to whom it concerns, and closes connections
    /// past their hello's deadline or whose token was revoked.
    void serve(execution::MonotonicInstant now);

    struct Statistics {
        std::uint64_t accepted = 0;
        std::uint64_t admitted = 0;
        /// Connections closed for a bad hello, a record past the limit,
        /// silence past the deadline, or a revoked token.
        std::uint64_t refused = 0;
        std::uint64_t records = 0;
    };
    [[nodiscard]] Statistics statistics() const noexcept;

    struct State;

private:
    explicit AuthoringHost(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

/// Where and to whom a session is hosted.
struct HostSettings {
    /// `host:port` to listen at.
    std::string endpoint;
    std::filesystem::path tokens;
    /// Where the certificate's fingerprint is written, for collaborators
    /// to pin.
    std::filesystem::path fingerprintFile;
    std::optional<std::filesystem::path> cooks;
    /// Collaborators connected at once, 1 to 64.
    std::size_t collaborators = 16;
};

/// `session` hosted as the settings say until `stopping` answers true:
/// over QUIC, under a self-signed certificate made for this run, whose
/// fingerprint is written where the settings say once it listens.
/// Refuses what `AuthoringHost::create` refuses, a network that does not
/// start or listen, and a fingerprint file that cannot be written.
[[nodiscard]] result::Status
hostSession(Session& session, const HostSettings& settings, const std::function<bool()>& stopping);

} // namespace rawframe::authoring_session
