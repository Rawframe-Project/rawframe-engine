#include "rawframe/authoring/authored_lines.h"

#include "rawframe/authoring/errors.h"

#include <utility>

namespace rawframe::authoring {

namespace {

using document::Value;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return result::fail(errorClass, kAuthoringDomain, code(error), why);
}

/// `text` as its lines, a last line end ending the last rather than
/// beginning another.
std::vector<std::string> linesOf(std::string_view text) {
    std::vector<std::string> made;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t kEnd = std::min(text.find('\n', at), text.size());
        made.emplace_back(text.substr(at, kEnd - at));
        at = kEnd + 1;
    }
    return made;
}

std::string written(const std::vector<std::string>& lines) {
    std::string made;
    for (const std::string& kLine : lines) {
        made += kLine;
        made += '\n';
    }
    return made;
}

std::size_t bytesOf(const std::vector<std::string>& lines) noexcept {
    std::size_t made = 0;
    for (const std::string& kLine : lines) {
        made += kLine.size() + 1;
    }
    return made;
}

} // namespace

result::Result<std::unique_ptr<AuthoredLines>>
AuthoredLines::open(base::Bits128 document, std::string_view text, DocumentReader reads, const HistoryLimits& limits) {
    RAWFRAME_TRY(reads(text));
    return std::unique_ptr<AuthoredLines>{new AuthoredLines(document, linesOf(text), reads, limits)};
}

std::string AuthoredLines::text() const {
    return written(lines_);
}

result::Result<Committed> AuthoredLines::change(std::uint64_t generation, std::span<const Operation> operations) {
    if (generation != generation_) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::TargetStale,
                      "a change is asked against the document's generation");
    }
    if (operations.size() > kMaximumBatchOperations) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a batch has more operations than its limit");
    }
    std::vector<std::string> lines = lines_;
    std::size_t deltas = 0;
    std::string summary;
    for (std::size_t at = 0; at < operations.size(); ++at) {
        const auto kIndexed = [at](std::unexpected<result::Error> refused) {
            return std::unexpected<result::Error>{std::move(refused).error().withContext("index", std::to_string(at))};
        };
        const auto* set = std::get_if<SetLine>(&operations[at]);
        const auto* put = std::get_if<InsertLine>(&operations[at]);
        if (set == nullptr && put == nullptr) {
            return kIndexed(refuse(result::ErrorClass::InvalidArgument,
                                   AuthoringError::ValidationFailed,
                                   "a line document's operations are lines.set and lines.insert"));
        }
        const std::size_t kLine = set != nullptr ? set->line : put->line;
        const std::optional<std::string>& kText = set != nullptr ? set->text : std::optional<std::string>{put->text};
        // A place is a line it holds; one past its last only puts one in.
        if (kLine > lines.size() || (set != nullptr && kLine == lines.size())) {
            return kIndexed(
                refuse(result::ErrorClass::NotFound, AuthoringError::TargetNotFound, "the document has no such line"));
        }
        if (kText.has_value() && kText->find_first_of("\r\n") != std::string::npos) {
            return kIndexed(refuse(
                result::ErrorClass::InvalidArgument, AuthoringError::ValidationFailed, "a line holds no line end"));
        }
        if (set != nullptr && kText.has_value() && lines[kLine] == *kText) {
            continue;
        }
        if (put != nullptr) {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(kLine), *kText);
        } else if (kText.has_value()) {
            lines[kLine] = *kText;
        } else {
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(kLine));
        }
        if (deltas == 0) {
            summary = std::string{put != nullptr      ? "insert"
                                  : kText.has_value() ? "set"
                                                      : "remove"} +
                      " line " + std::to_string(kLine);
        }
        ++deltas;
    }
    if (deltas == 0) {
        return Committed{.generation = generation_, .deltas = 0};
    }
    if (deltas > 1) {
        summary += " and " + std::to_string(deltas - 1) + " more";
    }
    RAWFRAME_TRY(reads_(written(lines)));
    const std::size_t kBytes = bytesOf(lines_) + bytesOf(lines);
    history_.append(
        Entry{.before = std::move(lines_), .after = lines, .deltas = deltas, .summary = summary, .bytes = kBytes});
    lines_ = std::move(lines);
    ++generation_;
    return Committed{.generation = generation_, .deltas = deltas};
}

result::Result<Committed> AuthoredLines::step(std::uint64_t generation, bool undo) {
    if (generation != generation_) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::TargetStale,
                      "an undo or redo is asked against the document's generation");
    }
    if (undo ? !canUndo() : !canRedo()) {
        return refuse(result::ErrorClass::NotFound,
                      AuthoringError::TargetNotFound,
                      undo ? "there is nothing to undo" : "there is nothing to redo");
    }
    const Entry& kEntry = history_.next(undo);
    lines_ = undo ? kEntry.before : kEntry.after;
    const std::size_t kDeltas = kEntry.deltas;
    history_.stepped(undo);
    ++generation_;
    return Committed{.generation = generation_, .deltas = kDeltas};
}

result::Result<Committed> AuthoredLines::undo(std::uint64_t generation) {
    return step(generation, true);
}

result::Result<Committed> AuthoredLines::redo(std::uint64_t generation) {
    return step(generation, false);
}

result::Result<Value> answer(const AuthoredLines& lines, const Query& query) {
    if (!std::holds_alternative<ReadLines>(query)) {
        return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                     AuthoringError::ValidationFailed,
                                                     "the query does not read a line document")
                                                  .error()
                                                  .withContext("operation", declarationOf(query).name)};
    }
    Value held = Value::array();
    for (const std::string& kLine : lines.lines()) {
        held.push(Value::string(kLine));
    }
    Value made = Value::object();
    made.add("lines", std::move(held));
    return made;
}

} // namespace rawframe::authoring
