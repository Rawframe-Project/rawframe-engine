#include "hosting.h"

#include "rawframe/authoring_session/cooked_copy.h"
#include "rawframe/authoring_session/link.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/document/json.h"
#include "rawframe/host/main.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::author {

namespace {

void complain(std::string_view command, std::string_view why) {
    std::fprintf(stderr,
                 "rawframe-author %.*s: %.*s\n",
                 static_cast<int>(command.size()),
                 command.data(),
                 static_cast<int>(why.size()),
                 why.data());
}

void say(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

} // namespace

int hostGame(const std::filesystem::path& game,
             const std::filesystem::path& root,
             const std::filesystem::path& cook,
             const std::filesystem::path& thumbnail,
             const authoring_session::HostSettings& settings) {
    rawframe::host::installStopBridge();
    authoring_session::Session session{game, root, cook, thumbnail};
    const auto kHosted = authoring_session::hostSession(session, settings, [] {
        return rawframe::host::stopRequested().load(std::memory_order_acquire);
    });
    if (!kHosted.has_value()) {
        complain("host", kHosted.error().description());
        return 1;
    }
    return 0;
}

int join(const char* endpoint, const char* pinFile, const char* tokenFile) {
    std::string said;
    std::unique_ptr<authoring_session::HostLink> link =
        authoring_session::HostLink::open(endpoint, pinFile, tokenFile, said);
    if (link == nullptr && !said.starts_with('{')) {
        complain("join", said);
        return 1;
    }
    say(said);
    if (link == nullptr) {
        return 1;
    }
    int status = 0;
    std::string line;
    for (int each = std::fgetc(stdin); each != EOF; each = std::fgetc(stdin)) {
        if (each != '\n') {
            line.push_back(static_cast<char>(each));
            continue;
        }
        // A cook is answered when it ends: what is said of it written as
        // it comes, until its reply (D885).
        const auto kParsed = document::parse(line);
        const document::Value* kKind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
        if (kKind != nullptr && kKind->kind() == document::Value::Kind::String && *kKind->text() == "authoring.cook") {
            const document::Value* kId = kParsed->find("id");
            const std::string kAnswering = document::writeCompact(kId != nullptr ? *kId : document::Value{});
            line.clear();
            if (!link->send(document::writeCompact(*kParsed))) {
                complain("join", "the host closed");
                return 1;
            }
            for (bool answered = false; !answered;) {
                for (const std::string& kHeard : link->heard(1000)) {
                    say(kHeard);
                    const auto kSaid = document::parse(kHeard);
                    const document::Value* kSaidKind = kSaid.has_value() ? kSaid->find("kind") : nullptr;
                    const document::Value* kSaidId = kSaid.has_value() ? kSaid->find("id") : nullptr;
                    const bool kReplied = kSaidKind != nullptr && kSaidKind->kind() == document::Value::Kind::String &&
                                          *kSaidKind->text() == "authoring.reply" && kSaidId != nullptr &&
                                          document::writeCompact(*kSaidId) == kAnswering;
                    if (kReplied) {
                        answered = true;
                        status = authoring_session::answered(kHeard) ? status : 1;
                    }
                }
                if (!answered && link->closed()) {
                    complain("join", "the host closed");
                    return 1;
                }
            }
            continue;
        }
        const auto kReply = link->ask(line);
        line.clear();
        // What the host said before the reply, in the order it said it.
        for (const std::string& kHeard : link->heard(0)) {
            say(kHeard);
        }
        if (!kReply.has_value()) {
            complain("join", "the host closed");
            return 1;
        }
        say(*kReply);
        status = authoring_session::answered(*kReply) ? status : 1;
    }
    return status;
}

int copyCookedFrom(
    const char* endpoint, const char* pinFile, const char* tokenFile, std::string_view name, const char* into) {
    std::string said;
    std::unique_ptr<authoring_session::HostLink> link =
        authoring_session::HostLink::open(endpoint, pinFile, tokenFile, said);
    if (link == nullptr) {
        complain("copy-cooked", said);
        return 1;
    }
    std::int64_t asked = 0;
    const auto kCopied = authoring_session::copyCooked(
        [&link, &asked](document::Value record) {
            record.add("id", document::Value::integer(++asked));
            // What the host says unasked meanwhile is not this copy's.
            std::optional<std::string> reply = link->ask(document::writeCompact(record));
            static_cast<void>(link->heard(0));
            return reply.value_or(std::string{});
        },
        name,
        into);
    if (!kCopied.has_value()) {
        complain("copy-cooked", kCopied.error().description());
        return 1;
    }
    document::Value made = document::Value::object();
    made.add("kind", document::Value::string("copied"));
    made.add("copied", document::Value::integer(static_cast<std::int64_t>(kCopied->copied)));
    made.add("kept", document::Value::integer(static_cast<std::int64_t>(kCopied->kept)));
    made.add("bytes", document::Value::integer(static_cast<std::int64_t>(kCopied->bytes)));
    say(document::writeCompact(made));
    return 0;
}

} // namespace rawframe::author
