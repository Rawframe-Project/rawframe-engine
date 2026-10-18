#include "rawframe/document/json.h"
#include "shell.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::studio {

std::string ShellParticipant::answerLine(const std::string& line) {
    // A host joined answers as a session held here does (D883); one that
    // closed refuses, and Studio says so once (hearHost).
    bool ended = false;
    if (held_.joined != nullptr) {
        return held_.joined->answer(line, ended);
    }
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
    for (const std::string& kLine : held_.joined->poll()) {
        // A cook's progress and reply, as a session's own (D885).
        hearCookLine(kLine);
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
            // Who is there, what each has chosen, and what each holds:
            // the title bar and the Hierarchy say so (D884).
            held_.collaborators.clear();
            const Value* kAuthors = kParsed->find("authors");
            for (const Value& kEach : kAuthors != nullptr ? kAuthors->items() : std::span<const Value>{}) {
                Collaborating made;
                const auto kTextIn = [&kEach](const char* name) {
                    const Value* kFound = kEach.find(name);
                    return kFound != nullptr && kFound->kind() == Value::Kind::String ? *kFound->text() : std::string{};
                };
                made.author = kTextIn("author");
                made.document = kTextIn("document");
                if (const Value* kChosenThere = kEach.find("chosen"); kChosenThere != nullptr) {
                    for (const Value& kOne : kChosenThere->items()) {
                        if (kOne.kind() == Value::Kind::String) {
                            made.chosen.push_back(*kOne.text());
                        }
                    }
                }
                if (const Value* kClaims = kEach.find("claims"); kClaims != nullptr) {
                    for (const Value& kClaim : kClaims->items()) {
                        const Value* kDocument = kClaim.find("document");
                        const Value* kEntity = kClaim.find("entity");
                        if (kDocument != nullptr && kDocument->kind() == Value::Kind::String) {
                            made.claims.emplace_back(*kDocument->text(),
                                                     kEntity != nullptr && kEntity->kind() == Value::Kind::String
                                                         ? *kEntity->text()
                                                         : std::string{});
                        }
                    }
                }
                held_.collaborators.push_back(std::move(made));
            }
            showCollaborators();
            if (!scene_.empty()) {
                refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
            }
            std::size_t marked = 0;
            for (const std::string& kEntity : entities_) {
                marked += presenceMarks(kEntity).empty() ? 0 : 1;
            }
            emitter_.log(diagnostics::Severity::Info,
                         kPresenceShown,
                         "the others at the host shown",
                         {diagnostics::field("others", held_.avatars.size()), diagnostics::field("marked", marked)});
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

namespace {

/// An author's color, the same wherever they are shown and in every
/// collaborator's Studio.
std::uint32_t colorOf(std::string_view author) {
    constexpr std::array<std::uint32_t, 6> kPalette = {
        0xE07A5FFFU, 0x81B29AFFU, 0xF2CC8FFFU, 0x9D8DF1FFU, 0x5FA8D3FFU, 0xE5989BFFU};
    std::uint32_t hash = 2166136261U;
    for (const char kEach : author) {
        hash = (hash ^ static_cast<unsigned char>(kEach)) * 16777619U;
    }
    return kPalette[hash % kPalette.size()];
}

} // namespace

void ShellParticipant::showCollaborators() {
    clear(held_.avatars);
    if (held_.avatarsNode == ui::Node{}) {
        return;
    }
    for (const Collaborating& kOther : held_.collaborators) {
        if (kOther.author == held_.joinedAs) {
            continue;
        }
        auto made = box(held_.avatarsNode, ui::Layout{.shrink = 0, .padding = {6, 1, 6, 1}}, colorOf(kOther.author));
        if (!made.has_value()) {
            return;
        }
        static_cast<void>(words(*made, kOther.author, kBackground, 13));
        static_cast<void>(tree_->setName(*made, kOther.author));
        held_.avatars.push_back(*made);
    }
}

std::string ShellParticipant::presenceMarks(const std::string& entity) const {
    std::string marks;
    for (const Collaborating& kOther : held_.collaborators) {
        const bool kMine = kOther.author == held_.joinedAs;
        if (!kMine && kOther.document == scene_ && std::ranges::contains(kOther.chosen, entity)) {
            marks += "  [" + kOther.author + "]";
        }
        if (std::ranges::contains(kOther.claims, std::pair{scene_, entity})) {
            marks += kMine ? std::string{"  [claimed]"} : "  [locked by " + kOther.author + "]";
        }
    }
    return marks;
}

void ShellParticipant::claimChosen(bool claim) {
    if (held_.joined == nullptr || scene_.empty() || entity_.empty()) {
        say(claim ? "claim an entity chosen in a scene, joined to a host" : "nothing claimed to let go");
        return;
    }
    Value record = Value::object();
    record.add("kind", Value::string(claim ? "authoring.claim" : "authoring.release"));
    record.add("id", Value::integer(next()));
    record.add("scene", Value::string(scene_));
    record.add("entity", Value::string(entity_));
    const std::string kReply = ask(record);
    const std::string kName =
        std::ranges::find(entities_, entity_) != entities_.end()
            ? names_[static_cast<std::size_t>(std::ranges::find(entities_, entity_) - entities_.begin())]
            : entity_;
    say(answeredOf(kReply).done ? (claim ? "claimed " : "let go of ") + kName : answeredOf(kReply).message);
}

} // namespace rawframe::studio
