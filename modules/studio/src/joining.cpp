#include "rawframe/document/json.h"
#include "shell.h"

#include <span>
#include <string>

namespace rawframe::studio {

std::string ShellParticipant::answerLine(const std::string& line) {
    if (held_.joined != nullptr) {
        // A host that closed answers nothing; Studio shows what it last
        // read and says so once (hearHost).
        return held_.joined->ask(line).value_or(std::string{});
    }
    bool ended = false;
    return session_ != nullptr ? session_->answer(line, ended) : std::string{};
}

bool ShellParticipant::gameOpen() const noexcept {
    return session_ != nullptr || held_.joined != nullptr;
}

void ShellParticipant::hearHost() {
    if (held_.joined == nullptr) {
        return;
    }
    // Said once Studio logs, which it does not yet as the game opens.
    if (!held_.saidJoined) {
        held_.saidJoined = true;
        emitter_.log(
            diagnostics::Severity::Info,
            kJoined,
            "Studio joined a shared session's host",
            {diagnostics::field("author", held_.joinedAs), diagnostics::field("endpoint", held_.joinEndpoint)});
    }
    // What the author has open and chosen, said to the host as it changes,
    // so the others see it (ADR-0085 section 5).
    std::string presenting = scene_;
    for (const std::string& kPicked : selected_) {
        presenting += " " + kPicked;
    }
    if (!scene_.empty() && presenting != held_.presented) {
        held_.presented = presenting;
        Value present = Value::object();
        present.add("kind", Value::string("authoring.present"));
        present.add("id", Value::integer(next()));
        present.add("scene", Value::string(scene_));
        Value chosen = Value::array();
        for (const std::string& kPicked : selected_) {
            chosen.push(Value::string(kPicked));
        }
        present.add("entities", std::move(chosen));
        present.add("view", Value{});
        static_cast<void>(ask(present));
    }
    for (const std::string& kLine : held_.joined->heard(0)) {
        const auto kParsed = document::parse(kLine);
        const Value* kKind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
        if (kKind == nullptr || kKind->kind() != Value::Kind::String) {
            continue;
        }
        const auto kTextOf = [&](const char* name) {
            const Value* kFound = kParsed->find(name);
            return kFound != nullptr && kFound->kind() == Value::Kind::String ? *kFound->text() : std::string{};
        };
        if (*kKind->text() == "authoring.changed") {
            // Another author's change: what Studio shows of that document
            // read again from the host, which holds it.
            const std::string kAuthor = kTextOf("author");
            const std::string kDocument = kTextOf("document");
            emitter_.log(diagnostics::Severity::Info,
                         kHostHeard,
                         "another author changed a document",
                         {diagnostics::field("kind", "changed"),
                          diagnostics::field("author", kAuthor),
                          diagnostics::field("document", kDocument)});
            if (kDocument == scene_) {
                say(kAuthor + " changed " + kDocument);
                refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
            }
        } else if (*kKind->text() == "authoring.presence") {
            const Value* kAuthors = kParsed->find("authors");
            std::string others;
            std::size_t present = 0;
            for (const Value& kEach : kAuthors != nullptr ? kAuthors->items() : std::span<const Value>{}) {
                const Value* kName = kEach.find("author");
                if (kName == nullptr || kName->kind() != Value::Kind::String || *kName->text() == held_.joinedAs) {
                    continue;
                }
                others += (others.empty() ? "" : ", ") + *kName->text();
                ++present;
            }
            emitter_.log(diagnostics::Severity::Info,
                         kHostHeard,
                         "who is present at the host",
                         {diagnostics::field("kind", "presence"), diagnostics::field("others", present)});
            if (!others.empty()) {
                say("with " + others);
            }
        } else if (*kKind->text() == "authoring.revoked") {
            say("the host's owner let " + held_.joinedAs + " go");
        }
    }
    if (held_.joined->closed() && !held_.toldClosed) {
        held_.toldClosed = true;
        say("the host closed the session");
        emitter_.log(diagnostics::Severity::Warning,
                     kHostHeard,
                     "the host closed the session",
                     {diagnostics::field("kind", "closed")});
    }
}

} // namespace rawframe::studio
