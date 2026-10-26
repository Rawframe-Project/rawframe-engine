#include "rawframe/authoring_session/host.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/session.h"
#include "rawframe/base/files.h"
#include "rawframe/base/secret.h"
#include "rawframe/document/json.h"
#include "rawframe/network_quic/quic.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <system_error>
#include <thread>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

/// A tokens file's bytes at most.
constexpr std::size_t kMostTokensBytes = std::size_t{1} << 20U;
/// How long a connection may go without a hello that admits it, how long
/// a closing one waits for its last line to be read, how often presence
/// is said at most, and how often the tokens file is looked at.
constexpr auto kHelloWithin = execution::MonotonicDuration::fromSeconds(5);
constexpr auto kLastLineWithin = execution::MonotonicDuration::fromSeconds(1);
constexpr auto kPresenceEvery = execution::MonotonicDuration::fromMilliseconds(100);
constexpr auto kTokensEvery = execution::MonotonicDuration::fromMilliseconds(250);

std::unexpected<result::Error> invalid(std::string_view why, std::size_t line) {
    return std::unexpected{result::fail(result::ErrorClass::InvalidArgument,
                                        authoring::kAuthoringDomain,
                                        code(authoring::AuthoringError::ValidationFailed),
                                        why)
                               .error()
                               .withContext("line", std::to_string(line))};
}

/// Which grant a record's kind needs; none for a kind the host itself
/// answers or refuses.
enum class Needs : std::uint8_t {
    Read,
    Edit,
    Cook,
    Play,
    Claim,
    Host,
};

struct KindNeeds {
    std::string_view kind;
    Needs needs;
    /// Whether the record is its author's, the host naming them.
    bool authored = false;
    /// Whether, kept, it changes a document the others are told of.
    bool changes = false;
    /// Whether it changes presence or claims.
    bool presence = false;
};

constexpr std::array kKinds = {
    KindNeeds{.kind = "authoring.describe", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.read", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.history", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.assets", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.bookmarks", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.extensions", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.extension_section", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.extension_panel", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.import_settings", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.thumbnail", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.mesh_bounds", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.asset_references", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.outline", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.presence", .needs = Needs::Read},
    KindNeeds{.kind = "authoring.present", .needs = Needs::Read, .authored = true, .presence = true},
    KindNeeds{.kind = "authoring.leave", .needs = Needs::Read, .authored = true, .presence = true},
    KindNeeds{.kind = "authoring.apply", .needs = Needs::Edit, .authored = true, .changes = true},
    KindNeeds{.kind = "authoring.apply_together", .needs = Needs::Edit, .authored = true, .changes = true},
    KindNeeds{.kind = "authoring.undo", .needs = Needs::Edit, .authored = true, .changes = true},
    KindNeeds{.kind = "authoring.redo", .needs = Needs::Edit, .authored = true, .changes = true},
    KindNeeds{.kind = "authoring.extension", .needs = Needs::Edit, .authored = true, .changes = true},
    KindNeeds{.kind = "authoring.create_scene", .needs = Needs::Edit, .changes = true},
    KindNeeds{.kind = "authoring.set_import_settings", .needs = Needs::Edit, .changes = true},
    KindNeeds{.kind = "authoring.move_file", .needs = Needs::Edit, .changes = true},
    KindNeeds{.kind = "authoring.remove_file", .needs = Needs::Edit, .changes = true},
    KindNeeds{.kind = "authoring.make_folder", .needs = Needs::Edit, .changes = true},
    KindNeeds{.kind = "authoring.select", .needs = Needs::Edit},
    KindNeeds{.kind = "authoring.view", .needs = Needs::Edit},
    KindNeeds{.kind = "authoring.bookmark", .needs = Needs::Edit},
    KindNeeds{.kind = "authoring.cook", .needs = Needs::Cook},
    KindNeeds{.kind = "authoring.cancel", .needs = Needs::Cook},
    KindNeeds{.kind = "authoring.cooked_file", .needs = Needs::Cook},
    KindNeeds{.kind = "authoring.pick", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.cast", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.mark", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.preview_input", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.hide_layers", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.frame", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.view_mode", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.split", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.overlays", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.run", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.step", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.traffic", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.profile", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.drawn_frame", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.live_entities", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.live_entity", .needs = Needs::Play},
    KindNeeds{.kind = "authoring.claim", .needs = Needs::Claim, .authored = true, .presence = true},
    KindNeeds{.kind = "authoring.release", .needs = Needs::Claim, .authored = true, .presence = true},
    // The host's own: a hello again, and the end of a connection, never of
    // the session. `import` and `preview` name files on the host's machine
    // by their paths, which a collaborator's record never does.
    KindNeeds{.kind = "authoring.hello", .needs = Needs::Host},
    KindNeeds{.kind = "authoring.end", .needs = Needs::Host},
    KindNeeds{.kind = "authoring.import", .needs = Needs::Host},
    KindNeeds{.kind = "authoring.preview", .needs = Needs::Host},
};

bool granted(const Grants& grants, Needs needs) noexcept {
    switch (needs) {
    case Needs::Read:
        return grants.read;
    case Needs::Edit:
        return grants.edit;
    case Needs::Cook:
        return grants.cook;
    case Needs::Play:
        return grants.play;
    case Needs::Claim:
        return grants.claim;
    case Needs::Host:
        return false;
    }
    return false;
}

Value grantNames(const Grants& grants) {
    Value made = Value::array();
    const std::array<std::pair<bool, const char*>, 5> kNamed{{{grants.read, "read"},
                                                              {grants.edit, "edit"},
                                                              {grants.cook, "cook"},
                                                              {grants.play, "play"},
                                                              {grants.claim, "claim"}}};
    for (const auto& [kHeld, kName] : kNamed) {
        if (kHeld) {
            made.push(Value::string(kName));
        }
    }
    return made;
}

std::string line(const Value& value) {
    return document::writeCompact(value) + "\n";
}

std::string replyLine(const Value& id, const char* member, Value inside) {
    Value made = Value::object();
    made.add("kind", Value::string("authoring.reply"));
    made.add("id", id);
    made.add(member, std::move(inside));
    return line(made);
}

std::string refusalLine(const Value& id, authoring::AuthoringError error, std::string_view why) {
    const auto kClass = error == authoring::AuthoringError::LimitExceeded      ? result::ErrorClass::ResourceExhausted
                        : error == authoring::AuthoringError::CapabilityDenied ? result::ErrorClass::PermissionDenied
                                                                               : result::ErrorClass::InvalidArgument;
    return authoring::writeRefusal(id, result::fail(kClass, authoring::kAuthoringDomain, code(error), why).error());
}

/// The bytes and the time a file was last written, to know it changed.
struct Stamp {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type written;
    friend bool operator==(const Stamp&, const Stamp&) = default;
};

std::optional<Stamp> stampOf(const std::filesystem::path& path) {
    std::error_code error;
    Stamp stamp{.size = std::filesystem::file_size(path, error), .written = {}};
    if (error) {
        return std::nullopt;
    }
    stamp.written = std::filesystem::last_write_time(path, error);
    return error ? std::nullopt : std::optional{stamp};
}

result::Result<std::vector<Collaborator>> readTokensFile(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || std::filesystem::file_size(path, error) > kMostTokensBytes) {
        return invalid("the tokens file is a regular file of at most 1 MiB", 0);
    }
    std::ifstream file{path, std::ios::binary};
    std::string text(kMostTokensBytes, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    return readCollaborators(text);
}

/// Whether `text` is a plain name, a collaborator's or a cook's output's:
/// 1 to 64 letters, digits, `.`, `_`, and `-`, not starting with `.`, so
/// it names one entry of a directory and nothing beside or above it.
bool plainName(std::string_view text) {
    return !text.empty() && text.size() <= 64 && text.front() != '.' && std::ranges::all_of(text, [](char kEach) {
        return (kEach >= 'a' && kEach <= 'z') || (kEach >= 'A' && kEach <= 'Z') || (kEach >= '0' && kEach <= '9') ||
               kEach == '.' || kEach == '_' || kEach == '-';
    });
}

} // namespace

result::Result<std::vector<Collaborator>> readCollaborators(std::string_view text) {
    std::vector<Collaborator> read;
    std::size_t number = 0;
    for (std::size_t start = 0; start < text.size();) {
        const std::size_t kEnd = std::min(text.find('\n', start), text.size());
        std::string_view each = text.substr(start, kEnd - start);
        start = kEnd + 1;
        ++number;
        if (!each.empty() && each.back() == '\r') {
            each.remove_suffix(1);
        }
        if (each.empty() || each.front() == '#') {
            continue;
        }
        std::vector<std::string_view> words;
        for (std::size_t at = 0; at < each.size();) {
            const std::size_t kSpace = std::min(each.find(' ', at), each.size());
            if (kSpace > at) {
                words.push_back(each.substr(at, kSpace - at));
            }
            at = kSpace + 1;
        }
        const auto kPrintable = [](std::string_view word) {
            return std::ranges::all_of(word, [](char kEach) {
                return static_cast<unsigned char>(kEach) > 0x20 && kEach != 0x7f;
            });
        };
        if (words.size() < 3 || words[0].size() < 32 || words[0].size() > 256 || !kPrintable(words[0]) ||
            !plainName(words[1])) {
            return invalid("a token line is a token of 32 to 256 bytes, a plain name, and its grants", number);
        }
        Collaborator made{.token = std::string{words[0]}, .name = std::string{words[1]}, .grants = {}};
        for (const std::string_view kGrant : std::span{words}.subspan(2)) {
            bool* held = kGrant == "read"    ? &made.grants.read
                         : kGrant == "edit"  ? &made.grants.edit
                         : kGrant == "cook"  ? &made.grants.cook
                         : kGrant == "play"  ? &made.grants.play
                         : kGrant == "claim" ? &made.grants.claim
                                             : nullptr;
            if (held == nullptr) {
                return invalid("a grant is read, edit, cook, play, or claim", number);
            }
            *held = true;
        }
        const bool kTwice = std::ranges::any_of(read, [&made](const Collaborator& kEach) {
            return base::sameSecret(kEach.token, made.token) || kEach.name == made.name;
        });
        if (kTwice) {
            return invalid("a token and a name are each given once", number);
        }
        if (read.size() == kMostCollaborators) {
            return invalid("a tokens file names at most 256 collaborators", number);
        }
        read.push_back(std::move(made));
    }
    return read;
}

network::ProviderProfile authoringProfile(std::size_t collaborators) noexcept {
    return network::ProviderProfile{.application = network::kAuthoringApplication,
                                    .maximumConnections = collaborators,
                                    .maximumStreamsPerConnection = 1,
                                    .maximumStreamSend = authoring::kMaximumSessionRecordBytes + 1,
                                    .maximumDatagram = 1,
                                    .maximumQueuedEvents = 1024,
                                    .maximumQueuedBytes = authoring::kMaximumSessionRecordBytes + 1};
}

struct AuthoringHost::State {
    struct Connected {
        execution::MonotonicInstant since;
        std::optional<network::StreamId> stream;
        std::string pending;
        /// The collaborator its hello named, once admitted.
        std::optional<Collaborator> admitted;
        bool closing = false;
        std::optional<execution::MonotonicInstant> closingSince;
    };

    network::Provider* provider = nullptr;
    Session* session = nullptr;
    std::filesystem::path tokens;
    std::optional<std::filesystem::path> cooks;
    std::vector<Collaborator> collaborators;
    std::optional<Stamp> tokensStamp;
    execution::MonotonicInstant tokensLooked;
    std::map<std::uint64_t, Connected> connected;
    std::vector<network::Event> events;
    /// Whether presence changed since it was last said, and when it was.
    bool presenceChanged = false;
    std::optional<execution::MonotonicInstant> presenceSaid;
    /// Who asked for the running cook, to whom what it says goes.
    std::optional<std::uint64_t> cooking;
    Statistics statistics;

    void send(std::uint64_t connection, Connected& client, std::string_view text) {
        if (!client.stream.has_value()) {
            return;
        }
        const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(text.data()), text.size()};
        if (!provider->send(network::ConnectionId{connection}, *client.stream, kBytes).has_value()) {
            client.closing = true;
        }
    }

    void refuse(std::uint64_t connection,
                Connected& client,
                const Value& id,
                authoring::AuthoringError error,
                std::string_view why) {
        send(connection, client, refusalLine(id, error, why));
        client.closing = true;
        ++statistics.refused;
    }

    /// Every admitted collaborator who may read but the one `from` names
    /// told `said`.
    void tellOthers(std::optional<std::uint64_t> from, std::string_view said) {
        for (auto& [kConnection, other] : connected) {
            if (kConnection != from && other.admitted.has_value() && other.admitted->grants.read && !other.closing) {
                send(kConnection, other, said);
            }
        }
    }

    /// A collaborator gone: their presence and claims let go.
    void letGo(const Collaborator& collaborator) {
        Value leaving = Value::object();
        leaving.add("kind", Value::string("authoring.leave"));
        leaving.add("id", Value{});
        leaving.add("author", Value::string(collaborator.name));
        bool ended = false;
        static_cast<void>(session->answer(document::writeCompact(leaving), ended));
        presenceChanged = true;
    }

    void hello(std::uint64_t connection, Connected& client, const Value& record, const Value& id) {
        const Value* kKind = record.find("kind");
        const Value* kToken = record.find("token");
        const Value* kGeneration = record.find("surfaceGeneration");
        if (kKind == nullptr || kKind->kind() != Value::Kind::String || *kKind->text() != "authoring.hello" ||
            kToken == nullptr || kToken->kind() != Value::Kind::String || kGeneration == nullptr ||
            kGeneration->integer() != authoring::kSurfaceGeneration) {
            refuse(connection,
                   client,
                   id,
                   authoring::AuthoringError::ValidationFailed,
                   "a collaborator's first record is a hello in the host's surface generation with a token");
            return;
        }
        const auto kFound = std::ranges::find_if(collaborators, [&kToken](const Collaborator& kEach) {
            return base::sameSecret(*kToken->text(), kEach.token);
        });
        if (kFound == collaborators.end()) {
            refuse(connection,
                   client,
                   id,
                   authoring::AuthoringError::CapabilityDenied,
                   "the token is not one the host's owner issued");
            return;
        }
        client.admitted = *kFound;
        ++statistics.admitted;
        Value welcome = Value::object();
        welcome.add("kind", Value::string("authoring.welcome"));
        welcome.add("surfaceGeneration", Value::integer(authoring::kSurfaceGeneration));
        welcome.add("author", Value::string(kFound->name));
        welcome.add("grants", grantNames(kFound->grants));
        send(connection, client, replyLine(id, "answer", std::move(welcome)));
        presenceChanged = true;
    }

    /// One record of an admitted collaborator's, theirs, answered.
    void record(std::uint64_t connection, Connected& client, std::string_view text) {
        ++statistics.records;
        const Collaborator& kWho = *client.admitted;
        auto parsed =
            document::parse(text, document::ReadLimits{.maximumBytes = authoring::kMaximumSessionRecordBytes});
        // Fail closed: only a record the host knows the grant of reaches
        // the session, so a verb added later is refused until it is given
        // one here.
        if (!parsed.has_value() || parsed->kind() != Value::Kind::Object) {
            send(connection,
                 client,
                 refusalLine(
                     Value{}, authoring::AuthoringError::ValidationFailed, "a record is one JSON object a line"));
            return;
        }
        const Value* kId = parsed->find("id");
        const Value kAnswerId = kId != nullptr ? *kId : Value{};
        const Value* kKind = parsed->find("kind");
        const std::string kNamed = kKind != nullptr && kKind->kind() == Value::Kind::String ? *kKind->text() : "";
        const auto kNeeds = std::ranges::find(kKinds, kNamed, &KindNeeds::kind);
        if (kNeeds == kKinds.end()) {
            send(
                connection,
                client,
                refusalLine(kAnswerId, authoring::AuthoringError::UnsupportedOperation, "the host has no such record"));
            return;
        }
        if (kNamed == "authoring.end") {
            Value ended = Value::object();
            ended.add("kind", Value::string("authoring.ended"));
            send(connection, client, replyLine(kAnswerId, "answer", std::move(ended)));
            client.closing = true;
            return;
        }
        if (!granted(kWho.grants, kNeeds->needs)) {
            send(connection,
                 client,
                 refusalLine(kAnswerId,
                             authoring::AuthoringError::CapabilityDenied,
                             kNeeds->needs == Needs::Host ? "a collaborator's record never names the host's files"
                                                          : "the collaborator's token does not grant it"));
            return;
        }
        // A record is its sender's: the author the token names, and none
        // other.
        const Value* kAuthor = parsed->find("author");
        if (kAuthor != nullptr && (kAuthor->kind() != Value::Kind::String || *kAuthor->text() != kWho.name)) {
            send(connection,
                 client,
                 refusalLine(kAnswerId, authoring::AuthoringError::CapabilityDenied, "a record is its sender's own"));
            return;
        }
        std::string asked{text};
        if (kNamed == "authoring.cook" || kNamed == "authoring.cooked_file") {
            // A collaborator names its cook's output and cache by plain
            // names, which the host puts under its cooking directory, in
            // the collaborator's own: no record names a path on the host's
            // machine; and reads it back by the same name.
            const std::optional<std::string> kCook = cookedUnder(*parsed, kWho.name);
            if (!kCook.has_value()) {
                send(connection,
                     client,
                     refusalLine(kAnswerId,
                                 authoring::AuthoringError::CapabilityDenied,
                                 cooks.has_value() ? "a collaborator's cook names its output and cache by plain names"
                                                   : "the host offers no cooking"));
                return;
            }
            asked = *kCook;
        }
        if (kNeeds->authored && kAuthor == nullptr) {
            Value owned = Value::object();
            for (const std::string& kName : parsed->names()) {
                owned.add(kName, *parsed->find(kName));
            }
            owned.add("author", Value::string(kWho.name));
            asked = document::writeCompact(owned);
        }
        bool ended = false;
        const std::string kReply = session->answer(asked, ended);
        if (kNamed == "authoring.cook" && kReply.empty()) {
            cooking = connection;
        }
        send(connection, client, kReply);
        if (kNeeds->presence) {
            presenceChanged = true;
        }
        if (kNeeds->changes) {
            told(connection, kWho.name, *parsed, kReply);
        }
    }

    /// A collaborator's cook record, or one reading a cook's file back, as
    /// the session reads it: its output and cache, plain names, as paths
    /// under the host's cooking directory, in the author's own; none for
    /// one that names anything else. A cook without a cache the session
    /// refuses.
    [[nodiscard]] std::optional<std::string> cookedUnder(const Value& record, const std::string& author) const {
        const Value* kOutput = record.find("output");
        const Value* kCache = record.find("cache");
        const bool kCacheNamed = kCache != nullptr && kCache->kind() == Value::Kind::String;
        if (!cooks.has_value() || kOutput == nullptr || kOutput->kind() != Value::Kind::String ||
            !plainName(*kOutput->text()) || (kCache != nullptr && !kCache->isNull() && !kCacheNamed) ||
            (kCacheNamed && !plainName(*kCache->text()))) {
            return std::nullopt;
        }
        const std::filesystem::path kOwn = *cooks / author;
        Value made = Value::object();
        for (const std::string& kName : record.names()) {
            if (kName == "output") {
                made.add(kName, Value::string((kOwn / *kOutput->text()).generic_string()));
            } else if (kName == "cache" && kCacheNamed) {
                made.add(kName, Value::string((kOwn / *kCache->text()).generic_string()));
            } else {
                made.add(kName, *record.find(kName));
            }
        }
        return document::writeCompact(made);
    }

    /// The others told of each document a record changed, by the digest it
    /// now has.
    void told(std::uint64_t from, const std::string& author, const Value& record, std::string_view reply) {
        const auto kParsed = document::parse(reply);
        const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        if (kAnswer == nullptr) {
            return;
        }
        const auto kTell = [&](const Value& outcome, const Value* scene) {
            const Value* kWritten = outcome.find("written");
            const Value* kDigest = outcome.find("document");
            if (scene == nullptr || scene->kind() != Value::Kind::String || kWritten == nullptr ||
                !kWritten->truth().value_or(false) || kDigest == nullptr) {
                return;
            }
            Value changed = Value::object();
            changed.add("kind", Value::string("authoring.changed"));
            changed.add("author", Value::string(author));
            changed.add("document", *scene);
            changed.add("digest", *kDigest);
            tellOthers(from, line(changed));
        };
        if (const Value* kDocuments = kAnswer->find("documents");
            kDocuments != nullptr && kDocuments->kind() == Value::Kind::Array) {
            for (const Value& kEach : kDocuments->items()) {
                kTell(kEach, kEach.find("scene"));
            }
            return;
        }
        // A new scene is written whole, its digest its first.
        if (const Value* kKind = kAnswer->find("kind");
            kKind != nullptr && kKind->kind() == Value::Kind::String && *kKind->text() == "authoring.created") {
            Value changed = Value::object();
            changed.add("kind", Value::string("authoring.changed"));
            changed.add("author", Value::string(author));
            changed.add("document", record.find("scene") != nullptr ? *record.find("scene") : Value{});
            changed.add("digest", Value{});
            tellOthers(from, line(changed));
            return;
        }
        kTell(*kAnswer, record.find("scene"));
    }

    void bytes(std::uint64_t connection, Connected& client, const network::Event& event) {
        // One stream a collaborator: the first it opens carries every
        // record.
        if (!client.stream.has_value()) {
            if (!event.stream.openedByConnector() || event.stream.unidirectional()) {
                client.closing = true;
                ++statistics.refused;
                return;
            }
            client.stream = event.stream;
        } else if (!(*client.stream == event.stream)) {
            refuse(
                connection, client, Value{}, authoring::AuthoringError::ValidationFailed, "one stream a collaborator");
            return;
        }
        client.pending.append(reinterpret_cast<const char*>(event.bytes.data()), event.bytes.size());
        std::size_t start = 0;
        for (std::size_t end = client.pending.find('\n'); end != std::string::npos && !client.closing;
             end = client.pending.find('\n', start)) {
            const std::string_view kLine = std::string_view{client.pending}.substr(start, end - start);
            start = end + 1;
            if (client.admitted.has_value()) {
                record(connection, client, kLine);
                continue;
            }
            const auto kParsed =
                document::parse(kLine, document::ReadLimits{.maximumBytes = authoring::kMaximumSessionRecordBytes});
            const Value* kId = kParsed.has_value() ? kParsed->find("id") : nullptr;
            hello(connection,
                  client,
                  kParsed.has_value() && kParsed->kind() == Value::Kind::Object ? *kParsed : Value::object(),
                  kId != nullptr ? *kId : Value{});
        }
        client.pending.erase(0, start);
        if (!client.closing && client.pending.size() > authoring::kMaximumSessionRecordBytes) {
            refuse(connection,
                   client,
                   Value{},
                   authoring::AuthoringError::LimitExceeded,
                   "a session record is at most 16 MiB");
        }
    }

    /// The tokens file read again where it changed; a collaborator whose
    /// token it no longer holds told so and let go at once.
    void lookAtTokens(execution::MonotonicInstant now) {
        if (now - tokensLooked < kTokensEvery) {
            return;
        }
        tokensLooked = now;
        const std::optional<Stamp> kStamp = stampOf(tokens);
        if (kStamp == tokensStamp) {
            return;
        }
        tokensStamp = kStamp;
        // A file that no longer reads holds no token.
        auto read = readTokensFile(tokens);
        collaborators = read.has_value() ? std::move(*read) : std::vector<Collaborator>{};
        for (auto& [kConnection, client] : connected) {
            if (!client.admitted.has_value() || client.closing) {
                continue;
            }
            const auto kStill = std::ranges::find_if(collaborators, [&client](const Collaborator& kEach) {
                return base::sameSecret(client.admitted->token, kEach.token) && kEach.name == client.admitted->name;
            });
            if (kStill == collaborators.end()) {
                Value revoked = Value::object();
                revoked.add("kind", Value::string("authoring.revoked"));
                revoked.add("author", Value::string(client.admitted->name));
                send(kConnection, client, line(revoked));
                client.closing = true;
                ++statistics.refused;
            } else {
                client.admitted->grants = kStill->grants;
            }
        }
    }

    void sayPresence(execution::MonotonicInstant now) {
        if (!presenceChanged || (presenceSaid.has_value() && now - *presenceSaid < kPresenceEvery)) {
            return;
        }
        presenceChanged = false;
        presenceSaid = now;
        bool ended = false;
        const std::string kReply = session->answer(R"({"kind":"authoring.presence","id":null})", ended);
        const auto kParsed = document::parse(kReply);
        const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        if (kAnswer != nullptr) {
            tellOthers(std::nullopt, line(*kAnswer));
        }
    }
};

result::Result<std::unique_ptr<AuthoringHost>> AuthoringHost::create(network::Provider& provider,
                                                                     Session& session,
                                                                     std::filesystem::path tokens,
                                                                     std::optional<std::filesystem::path> cooks) {
    auto state = std::make_unique<State>();
    state->provider = &provider;
    state->session = &session;
    state->tokens = std::move(tokens);
    if (cooks.has_value()) {
        state->cooks = std::filesystem::weakly_canonical(*cooks);
    }
    state->tokensStamp = stampOf(state->tokens);
    RAWFRAME_TRY_ASSIGN(state->collaborators, readTokensFile(state->tokens));
    bool ended = false;
    const std::string kWelcome = session.answer(R"({"kind":"authoring.hello","id":null,"surfaceGeneration":1})", ended);
    if (kWelcome.find("\"authoring.welcome\"") == std::string::npos) {
        return result::fail(result::ErrorClass::FailedPrecondition,
                            authoring::kAuthoringDomain,
                            code(authoring::AuthoringError::ValidationFailed),
                            "the session did not greet its host");
    }
    return std::unique_ptr<AuthoringHost>{new AuthoringHost{std::move(state)}};
}

AuthoringHost::AuthoringHost(std::unique_ptr<State> state) : state_(std::move(state)) {
}

AuthoringHost::~AuthoringHost() = default;

void AuthoringHost::serve(execution::MonotonicInstant now) {
    State& state = *state_;
    state.lookAtTokens(now);
    state.events.clear();
    state.provider->poll(state.events, 1024);
    for (const network::Event& event : state.events) {
        switch (event.kind) {
        case network::EventKind::Accepted:
            state.connected[event.connection.value] = State::Connected{.since = now};
            ++state.statistics.accepted;
            break;
        case network::EventKind::StreamBytes: {
            const auto kFound = state.connected.find(event.connection.value);
            if (kFound != state.connected.end() && !kFound->second.closing) {
                state.bytes(event.connection.value, kFound->second, event);
            }
            break;
        }
        case network::EventKind::Closed: {
            const auto kFound = state.connected.find(event.connection.value);
            if (kFound != state.connected.end()) {
                if (kFound->second.admitted.has_value()) {
                    state.letGo(*kFound->second.admitted);
                }
                state.connected.erase(kFound);
            }
            break;
        }
        case network::EventKind::Connected:
        case network::EventKind::Datagram:
            break;
        }
    }
    // What the session says unasked, a running cook's, to whoever asked.
    for (const std::string& kSaid : state.session->poll()) {
        const auto kAsker = state.cooking.has_value() ? state.connected.find(*state.cooking) : state.connected.end();
        if (kAsker != state.connected.end()) {
            state.send(kAsker->first, kAsker->second, kSaid);
        }
    }
    for (auto at = state.connected.begin(); at != state.connected.end();) {
        State::Connected& client = at->second;
        if (!client.admitted.has_value() && !client.closing && now - client.since > kHelloWithin) {
            state.refuse(
                at->first, client, Value{}, authoring::AuthoringError::ValidationFailed, "no hello came in time");
        }
        if (client.closing && !client.closingSince.has_value()) {
            client.closingSince = now;
        }
        if (client.closing && (!client.stream.has_value() || now - *client.closingSince > kLastLineWithin)) {
            if (client.admitted.has_value()) {
                state.letGo(*client.admitted);
            }
            state.provider->close(network::ConnectionId{at->first});
            at = state.connected.erase(at);
        } else {
            ++at;
        }
    }
    state.sayPresence(now);
}

AuthoringHost::Statistics AuthoringHost::statistics() const noexcept {
    return state_->statistics;
}

result::Status hostSession(Session& session, const HostSettings& settings, const std::function<bool()>& stopping) {
    if (settings.collaborators == 0 || settings.collaborators > 64) {
        return invalid("a host takes 1 to 64 collaborators at once", 0);
    }
    RAWFRAME_TRY_ASSIGN(network_quic::Certificate certificate,
                        network_quic::makeSelfSignedCertificate("rawframe-authoring", 30));
    RAWFRAME_TRY_ASSIGN(const network_quic::Fingerprint kFingerprint, network_quic::fingerprintOf(certificate));
    RAWFRAME_TRY_ASSIGN(
        std::unique_ptr<network_quic::QuicNetwork> network,
        network_quic::QuicNetwork::create(network_quic::QuicSettings{.certificate = std::move(certificate)}));
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<network::Provider> provider,
                        network->provider(authoringProfile(settings.collaborators)));
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<AuthoringHost> host,
                        AuthoringHost::create(*provider, session, settings.tokens, settings.cooks));
    RAWFRAME_TRY(provider->listen(network::Endpoint{settings.endpoint}));
    const std::string kPin = network_quic::formatFingerprint(kFingerprint) + "\n";
    if (!base::replaceFile(settings.fingerprintFile, std::as_bytes(std::span{kPin.data(), kPin.size()}))) {
        return invalid("the fingerprint file could not be written", 0);
    }
    const execution::SteadyClock kClock;
    while (!stopping()) {
        host->serve(kClock.now());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return {};
}

} // namespace rawframe::authoring_session
