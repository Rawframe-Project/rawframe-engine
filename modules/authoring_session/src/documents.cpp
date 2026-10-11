#include "styling.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/base/files.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, code(error), why).error();
}

/// The outcome record a scene's apply writes, of a sheet: nothing is
/// selected or looked from.
Value outcomeOf(
    const authoring::AuthoredStyles& styles, bool written, bool reopened, Value results, std::size_t skipped) {
    Value made = Value::object();
    made.add("kind", Value::string("authoring.outcome"));
    made.add("document", Value::string(digestOf(styles.text())));
    made.add("written", Value::boolean(written));
    made.add("reopened", Value::boolean(reopened));
    made.add("undoable", Value::integer(static_cast<std::int64_t>(styles.undoable())));
    made.add("redoable", Value::integer(static_cast<std::int64_t>(styles.redoable())));
    made.add("selection", Value::array());
    made.add("view", Value{});
    made.add("results", std::move(results));
    made.add("skipped", Value::integer(static_cast<std::int64_t>(skipped)));
    return made;
}

} // namespace

bool Styling::names(std::string_view document) noexcept {
    return std::filesystem::path{document}.extension() == ".styles";
}

result::Result<Styling::Open*> Styling::open(const std::string& name, bool& reopened) {
    reopened = false;
    const std::filesystem::path kPath = std::filesystem::weakly_canonical(root_ / name);
    const auto [kRootEnd, kPathAt] = std::ranges::mismatch(root_, kPath);
    const auto kText = kRootEnd == root_.end() && names(kPath.string()) ? readFile(kPath) : std::nullopt;
    if (!kText.has_value()) {
        return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "a session's style sheets are .styles files under its root that read")
                                   .withContext("scene", name)};
    }
    const std::string kDigest = digestOf(*kText);
    Open& open = sheets_[kPath.string()];
    if (open.document != nullptr && open.onDisk == kDigest) {
        return &open;
    }
    reopened = open.document != nullptr;
    RAWFRAME_TRY_ASSIGN(
        open.document,
        authoring::AuthoredStyles::open(sidecarIdentity(kPath).value_or(rawframe::base::Bits128{}), *kText));
    open.onDisk = kDigest;
    return &open;
}

result::Result<bool> Styling::save(const std::string& name, Open& open) {
    const std::string kText = open.document->text();
    if (digestOf(kText) == open.onDisk) {
        return false;
    }
    if (!base::replaceFile(std::filesystem::weakly_canonical(root_ / name),
                           std::as_bytes(std::span{kText.data(), kText.size()}))) {
        return std::unexpected{failure(authoring::AuthoringError::Internal,
                                       result::ErrorClass::Unavailable,
                                       "the style sheet could not be replaced")};
    }
    open.onDisk = digestOf(kText);
    open.document->markSaved();
    return true;
}

result::Result<Value> Styling::applied(const authoring::SessionRecord& record, bool& failed) {
    bool reopened = false;
    RAWFRAME_TRY_ASSIGN(Open * open, this->open(record.scene, reopened));
    authoring::AuthoredStyles& styles = *open->document;
    const authoring::Request& request = record.request;
    const std::string kBefore = digestOf(styles.text());
    if (request.expects.has_value() && *request.expects != kBefore) {
        return std::unexpected{failure(authoring::AuthoringError::TargetStale,
                                       result::ErrorClass::FailedPrecondition,
                                       "the request was computed against another generation of the style sheet")
                                   .withContext("document", kBefore)};
    }
    Value results = Value::array();
    std::size_t skipped = 0;
    if (request.batch == authoring::Batch::Atomic) {
        const auto kOutcome = styles.change(styles.generation(), request.operations);
        failed = !kOutcome.has_value();
        results.push(slotValue(kOutcome));
    } else {
        // Each operation its own entry, against what the one before left.
        for (std::size_t at = 0; at < request.operations.size(); ++at) {
            auto outcome = styles.change(styles.generation(), std::span{request.operations}.subspan(at, 1));
            const bool kFailed = !outcome.has_value();
            failed = failed || kFailed;
            results.push(slotValue(outcome));
            if (kFailed && request.batch == authoring::Batch::HaltRemaining) {
                skipped = request.operations.size() - at - 1;
                break;
            }
        }
    }
    RAWFRAME_TRY_ASSIGN(const bool kWritten, save(record.scene, *open));
    return outcomeOf(styles, kWritten, reopened, std::move(results), skipped);
}

result::Result<Value> Styling::stepped(const authoring::SessionRecord& record, bool undo, bool& failed) {
    bool reopened = false;
    RAWFRAME_TRY_ASSIGN(Open * open, this->open(record.scene, reopened));
    authoring::AuthoredStyles& styles = *open->document;
    const std::string kBefore = digestOf(styles.text());
    if (record.expects.has_value() && *record.expects != kBefore) {
        return std::unexpected{failure(authoring::AuthoringError::TargetStale,
                                       result::ErrorClass::FailedPrecondition,
                                       "the step was asked against another generation of the style sheet")
                                   .withContext("document", kBefore)};
    }
    const auto kStep = undo ? styles.undo(styles.generation()) : styles.redo(styles.generation());
    failed = !kStep.has_value();
    Value results = Value::array();
    results.push(slotValue(kStep));
    RAWFRAME_TRY_ASSIGN(const bool kWritten, save(record.scene, *open));
    return outcomeOf(styles, kWritten, reopened, std::move(results), 0);
}

result::Result<Value> Styling::answered(const authoring::SessionRecord& record, bool& failed) {
    bool reopened = false;
    RAWFRAME_TRY_ASSIGN(Open * open, this->open(record.scene, reopened));
    Value answers = Value::array();
    for (const authoring::Query& query : record.queries) {
        auto answer = authoring::answer(*open->document, query);
        Value made = Value::object();
        if (answer.has_value()) {
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

result::Result<Value> Styling::history(const authoring::SessionRecord& record) {
    bool reopened = false;
    RAWFRAME_TRY_ASSIGN(Open * open, this->open(record.scene, reopened));
    const authoring::AuthoredStyles& styles = *open->document;
    Value entries = Value::array();
    for (std::size_t at = 0; at < styles.undoable() + styles.redoable(); ++at) {
        const std::vector<authoring::JsonDelta>& kJournal = styles.journalAt(at);
        Value entry = Value::object();
        entry.add("summary", Value::string(authoring::summaryOf(kJournal)));
        entry.add("deltas", Value::integer(static_cast<std::int64_t>(kJournal.size())));
        entry.add("applied", Value::boolean(at < styles.undoable()));
        entries.push(std::move(entry));
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.history"));
    made.add("reopened", Value::boolean(reopened));
    made.add("entries", std::move(entries));
    return made;
}

} // namespace rawframe::authoring_session
