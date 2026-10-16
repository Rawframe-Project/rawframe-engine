#include "documents.h"

#include "rawframe/authoring/authored_data.h"
#include "rawframe/authoring/authored_material.h"
#include "rawframe/authoring/authored_styles.h"
#include "rawframe/authoring/authored_ui.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/base/files.h"
#include "rawframe/material/material.h"
#include "replies.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

/// The outcome record a scene's apply writes, of a JSON document: nothing
/// is selected or looked from.
template <class Authored>
Value outcomeOf(const Authored& authored, bool written, bool reopened, Value results, std::size_t skipped) {
    Value made = Value::object();
    made.add("kind", Value::string("authoring.outcome"));
    made.add("document", Value::string(digestOf(authored.text())));
    made.add("written", Value::boolean(written));
    made.add("reopened", Value::boolean(reopened));
    made.add("undoable", Value::integer(static_cast<std::int64_t>(authored.undoable())));
    made.add("redoable", Value::integer(static_cast<std::int64_t>(authored.redoable())));
    made.add("selection", Value::array());
    made.add("view", Value{});
    made.add("results", std::move(results));
    made.add("skipped", Value::integer(static_cast<std::int64_t>(skipped)));
    return made;
}

/// What an answer holds besides the document's own: nothing, but for a
/// material instance its base's interface, `null` where the base does not
/// read as a material, which says what each override may be (D721).
template <class Authored>
void addTo(Value& /*answer*/, const Authored& /*authored*/, const std::filesystem::path& /*root*/) {
}
void addTo(Value& answer, const authoring::AuthoredMaterial& material, const std::filesystem::path& root) {
    // A graph is its own interface's holder (D728).
    if (material.graph()) {
        return;
    }
    const std::filesystem::path kPath = std::filesystem::weakly_canonical(root / material.base());
    const auto [kRootEnd, kPathAt] = std::ranges::mismatch(root, kPath);
    const auto kText = kRootEnd == root.end() ? readFile(kPath) : std::nullopt;
    Value interface;
    if (kText.has_value() && material::readMaterial(*kText).has_value()) {
        if (auto read = document::parse(*kText); read.has_value() && read->find("interface") != nullptr) {
            interface = *read->find("interface");
        }
    }
    answer.add("interface", std::move(interface));
}

} // namespace

template <class Authored>
result::Result<typename Documents<Authored>::Open*> Documents<Authored>::open(const std::string& name, bool& reopened) {
    reopened = false;
    const std::filesystem::path kPath = std::filesystem::weakly_canonical(root_ / name);
    const auto [kRootEnd, kPathAt] = std::ranges::mismatch(root_, kPath);
    const auto kText = kRootEnd == root_.end() && names(kPath.string()) ? readFile(kPath) : std::nullopt;
    if (!kText.has_value()) {
        return std::unexpected{
            failure(authoring::AuthoringError::TargetNotFound,
                    result::ErrorClass::NotFound,
                    "a session's " + called_ + "s are " + extension_ + " files under its root that read")
                .withContext("scene", name)};
    }
    const std::string kDigest = digestOf(*kText);
    Open& open = opened_[kPath.string()];
    if (open.document != nullptr && open.onDisk == kDigest) {
        return &open;
    }
    reopened = open.document != nullptr;
    RAWFRAME_TRY_ASSIGN(open.document, made(sidecarIdentity(kPath).value_or(rawframe::base::Bits128{}), *kText));
    open.onDisk = kDigest;
    return &open;
}

template <class Authored>
result::Result<std::unique_ptr<Authored>> Documents<Authored>::made(rawframe::base::Bits128 identity,
                                                                    std::string_view text) const {
    if constexpr (std::is_same_v<Authored, authoring::AuthoredData>) {
        return Authored::open(identity, text, reads_);
    } else {
        return Authored::open(identity, text);
    }
}

template <class Authored> result::Result<bool> Documents<Authored>::save(const std::string& name, Open& open) {
    const std::string kText = open.document->text();
    if (digestOf(kText) == open.onDisk) {
        return false;
    }
    if (!base::replaceFile(std::filesystem::weakly_canonical(root_ / name),
                           std::as_bytes(std::span{kText.data(), kText.size()}))) {
        return std::unexpected{failure(authoring::AuthoringError::Internal,
                                       result::ErrorClass::Unavailable,
                                       "the " + called_ + " could not be replaced")};
    }
    open.onDisk = digestOf(kText);
    open.document->markSaved();
    return true;
}

template <class Authored>
result::Result<Value> Documents<Authored>::applied(const authoring::SessionRecord& record, bool& failed) {
    bool reopened = false;
    RAWFRAME_TRY_ASSIGN(Open * open, this->open(record.scene, reopened));
    Authored& authored = *open->document;
    const authoring::Request& request = record.request;
    const std::string kBefore = digestOf(authored.text());
    if (request.expects.has_value() && *request.expects != kBefore) {
        return std::unexpected{failure(authoring::AuthoringError::TargetStale,
                                       result::ErrorClass::FailedPrecondition,
                                       "the request was computed against another generation of the " + called_)
                                   .withContext("document", kBefore)};
    }
    // A dry run runs on a copy, so it answers as the request would be kept,
    // by the same code (D650).
    std::unique_ptr<Authored> copy;
    if (record.dryRun) {
        RAWFRAME_TRY_ASSIGN(copy, made(authored.identity(), authored.text()));
    }
    Authored& target = copy != nullptr ? *copy : authored;
    Value results = Value::array();
    std::size_t skipped = 0;
    if (request.batch == authoring::Batch::Atomic) {
        const auto kOutcome = target.change(target.generation(), request.operations);
        failed = !kOutcome.has_value();
        results.push(slotValue(kOutcome));
    } else {
        // Each operation its own entry, against what the one before left.
        for (std::size_t at = 0; at < request.operations.size(); ++at) {
            auto outcome = target.change(target.generation(), std::span{request.operations}.subspan(at, 1));
            const bool kFailed = !outcome.has_value();
            failed = failed || kFailed;
            results.push(slotValue(outcome));
            if (kFailed && request.batch == authoring::Batch::HaltRemaining) {
                skipped = request.operations.size() - at - 1;
                break;
            }
        }
    }
    if (copy != nullptr) {
        return ranDry(outcomeOf(authored, false, reopened, std::move(results), skipped), copy->text());
    }
    RAWFRAME_TRY_ASSIGN(const bool kWritten, save(record.scene, *open));
    return outcomeOf(authored, kWritten, reopened, std::move(results), skipped);
}

template <class Authored>
result::Result<Value> Documents<Authored>::stepped(const authoring::SessionRecord& record, bool undo, bool& failed) {
    bool reopened = false;
    RAWFRAME_TRY_ASSIGN(Open * open, this->open(record.scene, reopened));
    Authored& authored = *open->document;
    const std::string kBefore = digestOf(authored.text());
    if (record.expects.has_value() && *record.expects != kBefore) {
        return std::unexpected{failure(authoring::AuthoringError::TargetStale,
                                       result::ErrorClass::FailedPrecondition,
                                       "the step was asked against another generation of the " + called_)
                                   .withContext("document", kBefore)};
    }
    const auto kStep = undo ? authored.undo(authored.generation()) : authored.redo(authored.generation());
    failed = !kStep.has_value();
    Value results = Value::array();
    results.push(slotValue(kStep));
    RAWFRAME_TRY_ASSIGN(const bool kWritten, save(record.scene, *open));
    return outcomeOf(authored, kWritten, reopened, std::move(results), 0);
}

template <class Authored>
result::Result<Value> Documents<Authored>::answered(const authoring::SessionRecord& record, bool& failed) {
    bool reopened = false;
    RAWFRAME_TRY_ASSIGN(Open * open, this->open(record.scene, reopened));
    Value answers = Value::array();
    for (const authoring::Query& query : record.queries) {
        auto answer = authoring::answer(*open->document, query);
        Value made = Value::object();
        if (answer.has_value()) {
            addTo(*answer, *open->document, root_);
            made.add("answer", std::move(*answer));
        } else {
            failed = true;
            made.add("error", authoring::errorRecord(answer.error()));
        }
        answers.push(std::move(made));
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.answers"));
    made.add("document", Value::string(digestOf(open->document->text())));
    made.add("reopened", Value::boolean(reopened));
    made.add("answers", std::move(answers));
    return made;
}

template <class Authored> result::Result<Value> Documents<Authored>::history(const authoring::SessionRecord& record) {
    bool reopened = false;
    RAWFRAME_TRY_ASSIGN(Open * open, this->open(record.scene, reopened));
    const Authored& authored = *open->document;
    Value entries = Value::array();
    for (std::size_t at = 0; at < authored.undoable() + authored.redoable(); ++at) {
        const std::vector<authoring::JsonDelta>& kJournal = authored.journalAt(at);
        Value entry = Value::object();
        entry.add("summary", Value::string(authored.summaryAt(at)));
        entry.add("deltas", Value::integer(static_cast<std::int64_t>(kJournal.size())));
        entry.add("applied", Value::boolean(at < authored.undoable()));
        entries.push(std::move(entry));
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.history"));
    made.add("reopened", Value::boolean(reopened));
    made.add("entries", std::move(entries));
    return made;
}

template class Documents<authoring::AuthoredData>;
template class Documents<authoring::AuthoredMaterial>;
template class Documents<authoring::AuthoredStyles>;
template class Documents<authoring::AuthoredUi>;

} // namespace rawframe::authoring_session
