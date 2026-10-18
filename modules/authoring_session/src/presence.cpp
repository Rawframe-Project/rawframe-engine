#include "presence.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/schema/stable_id.h"
#include "replies.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

Value claimValue(const Claim& claim) {
    Value made = Value::object();
    made.add("document", Value::string(claim.document));
    made.add("entity", claim.entity.has_value() ? Value::string(*claim.entity) : Value{});
    return made;
}

result::Error claimedBy(std::string_view holder, std::string_view document) {
    return failure(authoring::AuthoringError::Conflict,
                   result::ErrorClass::Conflict,
                   "another author has claimed what the change touches")
        .withContext("author", holder)
        .withContext("document", document);
}

std::string idText(base::Bits128 id) {
    const auto kText = schema::formatStableIdText(id);
    return std::string{kText.data(), kText.size()};
}

} // namespace

result::Result<std::string> documentKey(const std::filesystem::path& root, const std::string& name) {
    const std::filesystem::path kPath = std::filesystem::weakly_canonical(root / name);
    const auto [kRootEnd, kPathAt] = std::ranges::mismatch(root, kPath);
    std::error_code error;
    if (kRootEnd != root.end() || !std::filesystem::is_regular_file(kPath, error)) {
        return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "a presence or a claim names a document under the session's root")
                                   .withContext("scene", name)};
    }
    return kPath.lexically_relative(root).generic_string();
}

std::vector<std::string> entitiesOf(const authoring::Journal& journal) {
    std::vector<std::string> touched;
    for (const authoring::Delta& kDelta : journal) {
        if (kDelta.kind == authoring::DeltaKind::SetMark) {
            continue;
        }
        std::string text = idText(kDelta.entity);
        if (!std::ranges::contains(touched, text)) {
            touched.push_back(std::move(text));
        }
    }
    return touched;
}

result::Result<Value> Presences::answer(const authoring::SessionRecord& record, const std::filesystem::path& root) {
    if (record.verb == authoring::SessionVerb::Presence) {
        return value();
    }
    Value made = Value::object();
    if (record.verb == authoring::SessionVerb::Leave) {
        leave(record.author);
        made.add("kind", Value::string("authoring.left"));
        made.add("author", Value::string(record.author));
        return made;
    }
    RAWFRAME_TRY_ASSIGN(std::string key, documentKey(root, record.scene));
    std::optional<std::string> entity;
    if (!record.entities.empty()) {
        entity = idText(record.entities.front());
    }
    if (record.verb == authoring::SessionVerb::Present) {
        Presence presence{.author = record.author, .document = key, .chosen = {}, .view = std::nullopt};
        for (const base::Bits128 kChosen : record.entities) {
            presence.chosen.push_back(idText(kChosen));
        }
        if (record.looking) {
            presence.view = record.view;
        }
        RAWFRAME_TRY(present(std::move(presence)));
        made.add("kind", Value::string("authoring.presented"));
    } else if (record.verb == authoring::SessionVerb::Claim) {
        RAWFRAME_TRY(claim(Claim{.author = record.author, .document = key, .entity = entity}));
        made.add("kind", Value::string("authoring.claimed"));
    } else {
        RAWFRAME_TRY(release(Claim{.author = record.author, .document = key, .entity = entity}));
        made.add("kind", Value::string("authoring.released"));
    }
    made.add("author", Value::string(record.author));
    made.add("document", Value::string(key));
    if (record.verb != authoring::SessionVerb::Present) {
        made.add("entity", entity.has_value() ? Value::string(*entity) : Value{});
    }
    return made;
}

result::Status Presences::present(Presence presence) {
    if (presence.chosen.size() > kMostChosen) {
        return std::unexpected{failure(authoring::AuthoringError::LimitExceeded,
                                       result::ErrorClass::ResourceExhausted,
                                       "a presence names at most 1024 entities chosen")};
    }
    const auto kHeld = std::ranges::find(present_, presence.author, &Presence::author);
    if (kHeld != present_.end()) {
        *kHeld = std::move(presence);
        return {};
    }
    if (present_.size() >= kMostPresent) {
        return std::unexpected{failure(authoring::AuthoringError::LimitExceeded,
                                       result::ErrorClass::ResourceExhausted,
                                       "a session holds at most 1024 authors' presence")};
    }
    present_.push_back(std::move(presence));
    return {};
}

result::Status Presences::claim(Claim claim) {
    for (const Claim& kHeld : claims_) {
        if (kHeld.document != claim.document) {
            continue;
        }
        if (kHeld.author == claim.author && kHeld.entity == claim.entity) {
            return {};
        }
        // Another's claim stands in the way where it holds the document
        // whole, this one would, or both name the entity.
        if (kHeld.author != claim.author &&
            (!kHeld.entity.has_value() || !claim.entity.has_value() || kHeld.entity == claim.entity)) {
            return std::unexpected{failure(authoring::AuthoringError::Conflict,
                                           result::ErrorClass::Conflict,
                                           "another author has claimed it")
                                       .withContext("author", kHeld.author)};
        }
    }
    if (claims_.size() >= kMostClaims) {
        return std::unexpected{failure(authoring::AuthoringError::LimitExceeded,
                                       result::ErrorClass::ResourceExhausted,
                                       "a session holds at most 4096 claims")};
    }
    claims_.push_back(std::move(claim));
    return {};
}

result::Status Presences::release(const Claim& claim) {
    const auto kHeld = std::ranges::find_if(claims_, [&claim](const Claim& each) {
        return each.author == claim.author && each.document == claim.document && each.entity == claim.entity;
    });
    if (kHeld == claims_.end()) {
        return std::unexpected{failure(
            authoring::AuthoringError::TargetNotFound, result::ErrorClass::NotFound, "the author holds no such claim")};
    }
    claims_.erase(kHeld);
    return {};
}

void Presences::leave(std::string_view author) {
    std::erase_if(present_, [author](const Presence& each) {
        return each.author == author;
    });
    std::erase_if(claims_, [author](const Claim& each) {
        return each.author == author;
    });
}

result::Status Presences::unclaimed(std::string_view author, std::string_view document) const {
    for (const Claim& kHeld : claims_) {
        if (kHeld.author != author && kHeld.document == document && !kHeld.entity.has_value()) {
            return std::unexpected{claimedBy(kHeld.author, document)};
        }
    }
    return {};
}

result::Status
Presences::untouched(std::string_view author, std::string_view document, std::span<const std::string> entities) const {
    for (const Claim& kHeld : claims_) {
        if (kHeld.author != author && kHeld.document == document && kHeld.entity.has_value() &&
            std::ranges::contains(entities, *kHeld.entity)) {
            return std::unexpected{claimedBy(kHeld.author, document)};
        }
    }
    return {};
}

bool Presences::othersHoldEntities(std::string_view author, std::string_view document) const {
    return std::ranges::any_of(claims_, [&](const Claim& each) {
        return each.author != author && each.document == document && each.entity.has_value();
    });
}

Value Presences::value() const {
    // Every author present or holding a claim, in the order they came.
    std::vector<std::string> authors;
    for (const Presence& kEach : present_) {
        authors.push_back(kEach.author);
    }
    for (const Claim& kEach : claims_) {
        if (!std::ranges::contains(authors, kEach.author)) {
            authors.push_back(kEach.author);
        }
    }
    Value listed = Value::array();
    for (const std::string& kAuthor : authors) {
        Value made = Value::object();
        made.add("author", Value::string(kAuthor));
        const auto kPresent = std::ranges::find(present_, kAuthor, &Presence::author);
        if (kPresent != present_.end()) {
            made.add("document", Value::string(kPresent->document));
            Value chosen = Value::array();
            for (const std::string& kEntity : kPresent->chosen) {
                chosen.push(Value::string(kEntity));
            }
            made.add("chosen", std::move(chosen));
            made.add("view", viewValue(kPresent->view));
        }
        Value claims = Value::array();
        for (const Claim& kClaim : claims_) {
            if (kClaim.author == kAuthor) {
                claims.push(claimValue(kClaim));
            }
        }
        made.add("claims", std::move(claims));
        listed.push(std::move(made));
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.presence"));
    made.add("authors", std::move(listed));
    return made;
}

} // namespace rawframe::authoring_session
