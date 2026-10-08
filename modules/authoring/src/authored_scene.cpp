#include "rawframe/authoring/authored_scene.h"

#include "rawframe/authoring/errors.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rawframe::authoring {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return result::fail(errorClass, kAuthoringDomain, code(error), why);
}

/// Of `selected`, in id order, the entities `scene` holds.
std::vector<base::Bits128> heldOf(const scene::Scene& scene, const std::vector<base::Bits128>& selected) {
    std::vector<base::Bits128> ids;
    ids.reserve(scene.entities.size());
    for (const scene::SceneEntity& entity : scene.entities) {
        ids.push_back(entity.id);
    }
    std::ranges::sort(ids);
    std::vector<base::Bits128> held;
    held.reserve(selected.size());
    for (const base::Bits128& each : selected) {
        if (std::ranges::binary_search(ids, each)) {
            held.push_back(each);
        }
    }
    return held;
}

/// What an entry's two selections weigh against the history's bytes.
std::size_t selectionBytes(const std::vector<base::Bits128>& before, const std::vector<base::Bits128>& after) {
    return (before.size() + after.size()) * sizeof(base::Bits128);
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

base::Bits128 Transaction::document() const noexcept {
    return scene_ != nullptr ? scene_->identity_ : base::Bits128{};
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
    open_ = Open{.staged = scene_,
                 .journal = {},
                 .journalBytes = 0,
                 .serial = ++serials_,
                 .coalescing = coalescing,
                 .selectedBefore = selection_,
                 .viewBefore = view_};
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
    selection_ = heldOf(scene_, selection_);
    append(Entry{.journal = std::move(open.journal),
                 .bytes = open.journalBytes + selectionBytes(open.selectedBefore, selection_) + 2 * sizeof(SceneView),
                 .selectedBefore = std::move(open.selectedBefore),
                 .selectedAfter = selection_,
                 .viewBefore = open.viewBefore,
                 .viewAfter = view_});
    return Committed{.generation = generation_, .deltas = kDeltas};
}

result::Result<std::vector<Committed>> commitTogether(std::span<Transaction> transactions) {
    const auto kCancelAll = [&] {
        for (Transaction& each : transactions) {
            each.cancel();
        }
    };
    for (std::size_t at = 0; at < transactions.size(); ++at) {
        const Transaction& kEach = transactions[at];
        if (!kEach.live() || !kEach.outermost_) {
            kCancelAll();
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "a multi-document transaction commits outermost transactions still open");
        }
        for (std::size_t before = 0; before < at; ++before) {
            if (transactions[before].scene_ == kEach.scene_) {
                kCancelAll();
                return refuse(result::ErrorClass::InvalidArgument,
                              AuthoringError::ValidationFailed,
                              "a multi-document transaction names each document once");
            }
        }
    }
    // Every staged scene in form before any is published: a commit checks
    // the same, so none fails after the first is kept.
    for (const Transaction& kEach : transactions) {
        const AuthoredScene::Open& kOpen = *kEach.scene_->open_;
        if (kOpen.journal.empty()) {
            continue;
        }
        if (auto written = scene::writeScene(kOpen.staged); !written.has_value()) {
            std::array<char, base::kBits128HexDigits> document{};
            base::formatBits128Hex(kEach.scene_->identity_, document);
            kCancelAll();
            return std::unexpected<result::Error>{
                std::move(written)
                    .error()
                    .mappedTo(result::ErrorClass::InvalidArgument,
                              kAuthoringDomain,
                              code(AuthoringError::ValidationFailed),
                              "a multi-document transaction leaves every scene in form")
                    .withContext("document", std::string_view{document.data(), document.size()})};
        }
    }
    std::vector<Committed> committed;
    for (Transaction& each : transactions) {
        RAWFRAME_TRY_ASSIGN(Committed one, each.commit());
        committed.push_back(one);
    }
    return committed;
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
    selection_ = heldOf(scene_, undo ? entry.selectedBefore : entry.selectedAfter);
    view_ = undo ? entry.viewBefore : entry.viewAfter;
    return Committed{.generation = generation_, .deltas = entry.journal.size()};
}

result::Status AuthoredScene::select(std::span<const base::Bits128> entities) {
    std::vector<base::Bits128> chosen{entities.begin(), entities.end()};
    std::ranges::sort(chosen);
    const auto kRepeated = std::ranges::unique(chosen);
    chosen.erase(kRepeated.begin(), kRepeated.end());
    if (chosen.size() > limits_.maximumSelection) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a selection holds at most its limit of entities");
    }
    // Inside a transaction, what it has staged: an entity it made can be
    // chosen at once.
    if (heldOf(open_.has_value() ? open_->staged : scene_, chosen).size() != chosen.size()) {
        return refuse(result::ErrorClass::NotFound,
                      AuthoringError::TargetNotFound,
                      "a selection holds only entities the scene holds");
    }
    selection_ = std::move(chosen);
    return {};
}

result::Status AuthoredScene::setView(const SceneView& view) {
    constexpr double kFarthest = 1e6;
    const auto kPlace = [](const std::array<double, 3>& at) {
        return std::ranges::all_of(at, [](double each) {
            return std::isfinite(each) && std::abs(each) <= kFarthest;
        });
    };
    if (!kPlace(view.eye) || !kPlace(view.target) || view.eye == view.target || !std::isfinite(view.fieldOfView) ||
        view.fieldOfView < 1 || view.fieldOfView > 179) {
        return refuse(result::ErrorClass::InvalidArgument,
                      AuthoringError::ValidationFailed,
                      "a view's eye and target are apart within a million metres, its field of view 1 to 179 degrees");
    }
    view_ = view;
    return {};
}

result::Result<Committed> AuthoredScene::undo(std::uint64_t generation) {
    return step(generation, true);
}

result::Result<Committed> AuthoredScene::redo(std::uint64_t generation) {
    return step(generation, false);
}

} // namespace rawframe::authoring
