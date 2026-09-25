#include "rawframe/authoring/authored_scene.h"

#include "rawframe/authoring/errors.h"

#include <utility>

namespace rawframe::authoring {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return result::fail(errorClass, kAuthoringDomain, code(error), why);
}

std::unexpected<result::Error> closed() {
    return refuse(result::ErrorClass::Conflict, AuthoringError::Conflict, "no transaction is open");
}

std::size_t bytesOf(const Delta& delta) {
    const auto kWritten = writeJournal({delta});
    return kWritten.has_value() ? kWritten->size() : 0;
}

bool sameSlot(const Delta& left, const Delta& right) {
    const bool kFoldable = left.kind == DeltaKind::Reorder || left.kind == DeltaKind::SetName ||
                           left.kind == DeltaKind::SetField || left.kind == DeltaKind::SetReference;
    return kFoldable && left.kind == right.kind && left.entity == right.entity && left.component == right.component &&
           left.field == right.field;
}

} // namespace

Transaction::Transaction(AuthoredScene* scene, std::uint64_t serial, bool outermost) noexcept
    : scene_(scene), serial_(serial), outermost_(outermost) {
}

Transaction::Transaction(Transaction&& other) noexcept
    : scene_(std::exchange(other.scene_, nullptr)), serial_(other.serial_), outermost_(other.outermost_) {
}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if (this != &other) {
        cancel();
        scene_ = std::exchange(other.scene_, nullptr);
        serial_ = other.serial_;
        outermost_ = other.outermost_;
    }
    return *this;
}

Transaction::~Transaction() {
    cancel();
}

bool Transaction::live() const noexcept {
    return scene_ != nullptr && scene_->open_.has_value() && scene_->open_->serial == serial_;
}

const scene::Scene& Transaction::staged() const noexcept {
    static const scene::Scene kNone;
    if (live()) {
        return scene_->open_->staged;
    }
    return scene_ != nullptr ? scene_->scene_ : kNone;
}

result::Status Transaction::stage(const Delta& delta) {
    if (!live()) {
        return closed();
    }
    return scene_->stage(delta);
}

result::Result<Committed> Transaction::commit() {
    if (!live()) {
        return closed();
    }
    AuthoredScene* scene = std::exchange(scene_, nullptr);
    if (!outermost_) {
        return Committed{.generation = scene->generation_, .deltas = 0};
    }
    return scene->commit();
}

void Transaction::cancel() noexcept {
    if (live()) {
        scene_->discard();
    }
    scene_ = nullptr;
}

result::Result<std::unique_ptr<AuthoredScene>>
AuthoredScene::open(base::Bits128 document, std::string_view text, const HistoryLimits& limits) {
    auto read = scene::readScene(text);
    if (!read.has_value()) {
        return std::unexpected<result::Error>{std::move(read).error().mappedTo(result::ErrorClass::InvalidArgument,
                                                                               kAuthoringDomain,
                                                                               code(AuthoringError::ValidationFailed),
                                                                               "an authored scene reads as a scene")};
    }
    std::unique_ptr<AuthoredScene> made{new AuthoredScene()};
    made->identity_ = document;
    made->scene_ = std::move(*read);
    made->limits_ = limits;
    return made;
}

std::string AuthoredScene::text() const {
    // Every generation was written once before it was published.
    return scene::writeScene(scene_).value_or("");
}

result::Result<Transaction> AuthoredScene::begin(std::uint64_t generation, Coalescing coalescing) {
    if (generation != generation_) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::TargetStale,
                      "a transaction opens against the document's generation");
    }
    if (open_.has_value()) {
        return Transaction{this, open_->serial, false};
    }
    open_ = Open{.staged = scene_, .journal = {}, .journalBytes = 0, .serial = ++serials_, .coalescing = coalescing};
    return Transaction{this, open_->serial, true};
}

result::Status AuthoredScene::stage(const Delta& delta) {
    Open& open = *open_;
    auto applied = apply(open.staged, delta, true);
    if (!applied.has_value()) {
        discard();
        return applied;
    }
    if (open.coalescing == Coalescing::FoldSlots && !open.journal.empty() && sameSlot(open.journal.back(), delta)) {
        Delta& last = open.journal.back();
        open.journalBytes -= bytesOf(last);
        last.after = delta.after;
        if (last.before == last.after) {
            open.journal.pop_back();
        } else {
            open.journalBytes += bytesOf(last);
        }
        return {};
    }
    open.journal.push_back(delta);
    open.journalBytes += bytesOf(delta);
    if (open.journal.size() > limits_.maximumJournalDeltas || open.journalBytes > limits_.maximumJournalBytes) {
        discard();
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a transaction's journal is past its limits");
    }
    return {};
}

result::Result<Committed> AuthoredScene::commit() {
    Open open = std::move(*open_);
    open_.reset();
    if (open.journal.empty()) {
        return Committed{.generation = generation_, .deltas = 0};
    }
    // The staged scene in form, or nothing is kept.
    auto written = scene::writeScene(open.staged);
    if (!written.has_value()) {
        return std::unexpected<result::Error>{
            std::move(written).error().mappedTo(result::ErrorClass::InvalidArgument,
                                                kAuthoringDomain,
                                                code(AuthoringError::ValidationFailed),
                                                "a transaction leaves its scene in form")};
    }
    scene_ = std::move(open.staged);
    ++generation_;
    const std::size_t kDeltas = open.journal.size();
    append(Entry{.journal = std::move(open.journal), .bytes = open.journalBytes});
    return Committed{.generation = generation_, .deltas = kDeltas};
}

void AuthoredScene::discard() noexcept {
    open_.reset();
}

void AuthoredScene::append(Entry entry) {
    // A new entry drops the redo side.
    while (history_.size() > applied_) {
        historyBytes_ -= history_.back().bytes;
        history_.pop_back();
    }
    if (clean_.has_value() && *clean_ > applied_) {
        clean_.reset();
    }
    historyBytes_ += entry.bytes;
    history_.push_back(std::move(entry));
    ++applied_;
    // Oldest first; a clean place evicted is definitely dirty.
    while (!history_.empty() && (history_.size() > limits_.maximumEntries || historyBytes_ > limits_.maximumBytes)) {
        historyBytes_ -= history_.front().bytes;
        history_.pop_front();
        --applied_;
        if (clean_.has_value()) {
            clean_ = *clean_ == 0 ? std::nullopt : std::optional{*clean_ - 1};
        }
    }
}

result::Result<Committed> AuthoredScene::step(std::uint64_t generation, bool undo) {
    if (generation != generation_) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::TargetStale,
                      "an undo or redo is asked against the document's generation");
    }
    if (open_.has_value()) {
        return refuse(result::ErrorClass::Conflict, AuthoringError::Conflict, "no undo or redo inside a transaction");
    }
    if (undo ? !canUndo() : !canRedo()) {
        return refuse(result::ErrorClass::NotFound,
                      AuthoringError::TargetNotFound,
                      undo ? "there is nothing to undo" : "there is nothing to redo");
    }
    const Entry& entry = history_[undo ? applied_ - 1 : applied_];
    auto applied = apply(scene_, entry.journal, !undo);
    if (!applied.has_value()) {
        // An entry is only ever applied to the scene it was made against.
        return std::unexpected<result::Error>{std::move(applied).error().mappedTo(
            result::ErrorClass::Internal, kAuthoringDomain, code(AuthoringError::Internal), "a history entry applies")};
    }
    applied_ = undo ? applied_ - 1 : applied_ + 1;
    ++generation_;
    return Committed{.generation = generation_, .deltas = entry.journal.size()};
}

result::Result<Committed> AuthoredScene::undo(std::uint64_t generation) {
    return step(generation, true);
}

result::Result<Committed> AuthoredScene::redo(std::uint64_t generation) {
    return step(generation, false);
}

} // namespace rawframe::authoring
