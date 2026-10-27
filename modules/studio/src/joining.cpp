#include "rawframe/document/json.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::studio {

std::string ShellParticipant::answerLine(const std::string& line) {
    // A host joined answers as a session held here does; one that
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

std::string ShellParticipant::previewedName() const {
    return scene_.empty() && gameOpen() ? description_.filename().string() : scene_;
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
    // so the others see it.
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
        // A scene's entities or a UI document's nodes; another
        // document's rows are not presence.
        Value picked = Value::array();
        if (chosen(Listed::Scene) || chosen(Listed::Ui)) {
            for (const std::string& kPicked : selected_) {
                picked.push(Value::string(kPicked));
            }
        }
        present.add(chosen(Listed::Ui) ? "nodes" : "entities", std::move(picked));
        present.add("view", Value{});
        static_cast<void>(ask(present));
    }
    for (const std::string& kLine : held_.joined->poll()) {
        // A cook's progress and reply, as a session's own.
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
            // the title bar and the Hierarchy say so.
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
                        // An entity's id or a UI node's component, the
                        // whole document where neither is named.
                        const Value* kDocument = kClaim.find("document");
                        const Value* kPart =
                            kClaim.find("node") != nullptr ? kClaim.find("node") : kClaim.find("entity");
                        if (kDocument != nullptr && kDocument->kind() == Value::Kind::String) {
                            made.claims.emplace_back(*kDocument->text(),
                                                     kPart != nullptr && kPart->kind() == Value::Kind::String
                                                         ? *kPart->text()
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
                         {diagnostics::field("others", held_.avatars.size() > 2 ? held_.avatars.size() - 2 : 0),
                          diagnostics::field("marked", marked)});
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

std::uint32_t authorColorOf(std::string_view author) {
    constexpr std::array<std::uint32_t, 6> kPalette = {
        0xE07A5FFFU, 0x81B29AFFU, 0xF2CC8FFFU, 0x9D8DF1FFU, 0x5FA8D3FFU, 0xE5989BFFU};
    std::uint32_t hash = 2166136261U;
    for (const char kEach : author) {
        hash = (hash ^ static_cast<unsigned char>(kEach)) * 16777619U;
    }
    return kPalette[hash % kPalette.size()];
}

namespace {

/// An author's initials, as the design's avatars show them: the first
/// two letters of a name of one word, else the first of each of two.
std::string initialsOf(std::string_view author) {
    std::string made;
    const std::size_t kSpace = author.find_first_of(" ._-");
    if (kSpace != std::string_view::npos && kSpace > 0 && kSpace + 1 < author.size()) {
        made = {author.front(), author[kSpace + 1]};
    } else {
        made = std::string{author.substr(0, 2)};
    }
    for (char& each : made) {
        each = static_cast<char>(std::toupper(static_cast<unsigned char>(each)));
    }
    return made;
}

} // namespace

result::Result<ui::Node> ShellParticipant::avatar(ui::Node parent, std::string_view author, float size) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kMade,
                        box(parent,
                            ui::Layout{.width = ui::pixels(size),
                                       .height = ui::pixels(size),
                                       .justify = ui::Justify::Center,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0},
                            0));
    RAWFRAME_TRY(tree_->setLook(kMade, ui::Look{.fill = authorColorOf(author), .radius = size / 2}));
    // The words in a child of their own height, which the circle centers.
    RAWFRAME_TRY_ASSIGN(const ui::Node kInitials, box(kMade, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(strongWords(kInitials, initialsOf(author), kBackground, size * 0.45F));
    RAWFRAME_TRY(tree_->setInteraction(kInitials, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    return kMade;
}

void ShellParticipant::showCollaborators() {
    clear(held_.avatars);
    if (held_.avatarsNode == ui::Node{}) {
        return;
    }
    // As the design's Together screen: how many are here, then each
    // author's initials in their color, this one's first, each pressed for
    // who is in the session and what each is doing.
    std::vector<std::string> authors = {held_.joinedAs};
    for (const Collaborating& kOther : held_.collaborators) {
        if (kOther.author != held_.joinedAs) {
            authors.push_back(kOther.author);
        }
    }
    const std::size_t kHere = authors.size();
    if (auto pill = box(held_.avatarsNode, ui::Layout{.shrink = 0, .padding = {7, 7, 2, 2}, .border = {1, 1, 1, 1}}, 0);
        pill.has_value()) {
        held_.avatars.push_back(*pill);
        static_cast<void>(
            tree_->setLook(*pill, ui::Look{.fill = 0x16301FFFU, .borderColor = 0x2F6B44FFU, .radius = 4}));
        static_cast<void>(
            monoWords(*pill, "joined · " + std::to_string(kHere) + " here", 0x7FD99BFFU, look::kSmallSize));
        static_cast<void>(tree_->setRole(*pill, ui::Role::Button));
        static_cast<void>(tree_->setName(*pill, "Session"));
    }
    for (const std::string& kAuthor : authors) {
        auto made = avatar(held_.avatarsNode, kAuthor, 22);
        if (!made.has_value()) {
            return;
        }
        static_cast<void>(tree_->setRole(*made, ui::Role::Button));
        static_cast<void>(tree_->setName(*made, kAuthor == held_.joinedAs ? "You: " + kAuthor : kAuthor));
        held_.avatars.push_back(*made);
    }
}

std::string ShellParticipant::doingOf(const Collaborating& other) const {
    // What the presence says each is doing: what they hold, what they
    // have chosen, or the document they have open.
    const auto kNamed = [this, &other](const std::string& entity) {
        const auto kAt = std::ranges::find(entities_, entity);
        return other.document == scene_ && kAt != entities_.end() &&
                       !names_[static_cast<std::size_t>(kAt - entities_.begin())].empty()
                   ? names_[static_cast<std::size_t>(kAt - entities_.begin())]
                   : std::string{"an entity"};
    };
    if (!other.claims.empty()) {
        return "editing " + (other.claims.front().second.empty()
                                 ? other.claims.front().first
                                 : kNamed(other.claims.front().second) + " in " + other.claims.front().first);
    }
    if (other.chosen.size() == 1) {
        return "on " + kNamed(other.chosen.front()) + " in " + other.document;
    }
    if (!other.chosen.empty()) {
        return std::to_string(other.chosen.size()) + " chosen in " + other.document;
    }
    return other.document.empty() ? std::string{"here"} : "viewing " + other.document;
}

bool ShellParticipant::pressSession(ui::Node node) {
    if (node == ui::Node{} || !std::ranges::contains(held_.avatars, node)) {
        return false;
    }
    const bool kWasOpen = popup_.has_value() && *popup_ == held_.sessionCard;
    closePopup();
    if (kWasOpen) {
        return true;
    }
    auto card = box(root_,
                    ui::Layout{.width = ui::pixels(320),
                               .direction = ui::Direction::Column,
                               .gap = 2,
                               .padding = {8, 8, 10, 10},
                               .border = {1, 1, 1, 1},
                               .placement = {.absolute = true}},
                    0);
    if (!card.has_value() || !tree_
                                  ->setPopup(*card,
                                             ui::Popup{.anchor = held_.avatarsNode,
                                                       .side = ui::Popup::Side::Below,
                                                       .gap = 6,
                                                       .margin = 4,
                                                       .align = ui::Popup::Align::End})
                                  .has_value()) {
        return true;
    }
    static_cast<void>(tree_->setLook(*card,
                                     ui::Look{.fill = 0x1E2026FFU,
                                              .borderColor = look::kRaisedEdge,
                                              .radius = 7,
                                              .outerShadow = {.color = 0x00000080U, .y = 6, .blur = 18}}));
    static_cast<void>(tree_->setRole(*card, ui::Role::Group));
    static_cast<void>(tree_->setName(*card, "In this session"));
    popup_ = *card;
    held_.sessionCard = *card;
    if (auto heading = box(*card, ui::Layout{.shrink = 0, .padding = {6, 6, 0, 6}}, 0); heading.has_value()) {
        static_cast<void>(words(*heading, "In this session", look::kFaintInk, look::kLabelSize));
    }
    const auto kAuthorRow = [this, &card](const std::string& author, const std::string& doing) {
        auto line =
            box(*card, ui::Layout{.alignItems = ui::Align::Center, .gap = 10, .shrink = 0, .padding = {6, 6, 5, 5}}, 0);
        if (!line.has_value()) {
            return;
        }
        static_cast<void>(avatar(*line, author, 28));
        if (auto said = box(*line, ui::Layout{.direction = ui::Direction::Column, .gap = 1, .grow = 1}, 0);
            said.has_value()) {
            if (auto named = box(*said, ui::Layout{.shrink = 0}, 0); named.has_value()) {
                static_cast<void>(
                    strongWords(*named, author == held_.joinedAs ? "You" : author, look::kInk, look::kBodySize));
            }
            if (auto what = box(*said, ui::Layout{.shrink = 0}, 0); what.has_value()) {
                static_cast<void>(words(*what, doing, look::kLabelInk, look::kLabelSize));
            }
        }
        static_cast<void>(tree_->setName(*line, author + ": " + doing));
    };
    bool sawSelf = false;
    for (const Collaborating& kOther : held_.collaborators) {
        sawSelf = sawSelf || kOther.author == held_.joinedAs;
        if (kOther.author == held_.joinedAs) {
            kAuthorRow(kOther.author, "joined · " + doingOf(kOther));
        }
    }
    if (!sawSelf) {
        kAuthorRow(held_.joinedAs, "joined");
    }
    for (const Collaborating& kOther : held_.collaborators) {
        if (kOther.author != held_.joinedAs) {
            kAuthorRow(kOther.author, doingOf(kOther));
        }
    }
    if (auto foot = box(
            *card, ui::Layout{.shrink = 0, .padding = {6, 6, 8, 2}, .margin = {0, 0, 4, 0}, .border = {0, 0, 1, 0}}, 0);
        foot.has_value()) {
        static_cast<void>(tree_->setLook(*foot, ui::Look{.borderColor = look::kRaisedEdge}));
        static_cast<void>(wrappedWords(*foot,
                                       "Hosted at " + held_.joinEndpoint +
                                           " by rawframe-author host; another author joins with rawframe-author "
                                           "join or Studio's studio.join settings, an agent with mcp --join.",
                                       look::kFaintInk,
                                       look::kSmallSize));
    }
    say("in this session: " + std::to_string(held_.avatars.size() > 0 ? held_.avatars.size() - 1 : 0));
    return true;
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
    if (held_.joined == nullptr || !(chosen(Listed::Scene) || chosen(Listed::Ui)) || entity_.empty()) {
        say(claim ? "claim an entity or a UI node chosen, joined to a host" : "nothing claimed to let go");
        return;
    }
    Value record = Value::object();
    record.add("kind", Value::string(claim ? "authoring.claim" : "authoring.release"));
    record.add("id", Value::integer(next()));
    record.add("scene", Value::string(scene_));
    record.add(chosen(Listed::Ui) ? "node" : "entity", Value::string(entity_));
    const std::string kReply = ask(record);
    const std::string kName =
        std::ranges::find(entities_, entity_) != entities_.end()
            ? names_[static_cast<std::size_t>(std::ranges::find(entities_, entity_) - entities_.begin())]
            : entity_;
    say(answeredOf(kReply).done ? (claim ? "claimed " : "let go of ") + kName : answeredOf(kReply).message);
}

} // namespace rawframe::studio
