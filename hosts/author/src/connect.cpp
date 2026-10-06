#include "connect.h"

#include "rawframe/authoring_session/link.h"
#include "rawframe/document/json.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace rawframe::author {

namespace {

void complain(std::string_view why) {
    std::fprintf(stderr, "rawframe-author connect: %.*s\n", static_cast<int>(why.size()), why.data());
}

/// Whether a record asks the endpoint to end the connection.
bool ending(std::string_view record) {
    const auto kParsed = document::parse(record);
    const document::Value* kKind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
    return kKind != nullptr && kKind->text() != nullptr && *kKind->text() == "tooling.end";
}

} // namespace

int connect(const char* endpoint, const char* pinFile, const char* tokenFile) {
    std::string said;
    std::unique_ptr<authoring_session::ToolingLink> link =
        authoring_session::ToolingLink::open(endpoint, pinFile, tokenFile, said);
    if (link == nullptr && !said.starts_with('{')) {
        complain(said);
        return 1;
    }
    std::printf("%s\n", said.c_str());
    std::fflush(stdout);
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
        const auto kReply = link->ask(line);
        const bool kEnded = ending(line);
        line.clear();
        // The endpoint closes on end, and the close may outrun its reply.
        if (!kReply.has_value() && kEnded) {
            return status;
        }
        if (!kReply.has_value()) {
            complain("the endpoint closed");
            return 1;
        }
        std::printf("%s\n", kReply->c_str());
        std::fflush(stdout);
        status = authoring_session::answered(*kReply) ? status : 1;
        if (kEnded) {
            return status;
        }
    }
    return status;
}

} // namespace rawframe::author
