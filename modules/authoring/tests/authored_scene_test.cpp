// Transactions and history (ADR-0065): all or nothing, bound to a
// generation, one entry a transaction, undo and redo exact, the clean place
// kept, and the history bounded without ever claiming a false clean.

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/test/test.h"

#include <memory>
#include <string>

using namespace rawframe;
using namespace rawframe::authoring;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

const base::Bits128 kDocument{7, 7};
const base::Bits128 kSpawn{1, 1};

scene::FieldValue number(std::string_view text) {
    return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = std::string{text}};
}

std::string levelText() {
    const scene::Scene kLevel{
        .schema = {{.component = "game.position", .mark = 0xa1}},
        .entities = {{.id = kSpawn,
                      .name = "spawn",
                      .components = {{.name = "game.position", .fields = {{.name = "x", .value = number("1")}}}}}},
        .instances = {}};
    return *scene::writeScene(kLevel);
}

std::unique_ptr<AuthoredScene> opened(const HistoryLimits& limits = {}) {
    auto made = AuthoredScene::open(kDocument, levelText(), limits);
    RAWFRAME_EXPECT(made.has_value());
    return std::move(*made);
}

/// x from one value to another.
Delta moveX(std::string_view from, std::string_view to) {
    Delta made{.kind = DeltaKind::SetField, .entity = kSpawn, .component = "game.position", .field = "x"};
    if (from != "0") {
        made.before.field = number(from);
    }
    if (to != "0") {
        made.after.field = number(to);
    }
    return made;
}

/// Commits x's move as one transaction against the document's generation.
void commitMove(AuthoredScene& scene, std::string_view from, std::string_view to) {
    auto transaction = scene.begin(scene.generation());
    RAWFRAME_EXPECT(transaction.has_value() && transaction->stage(moveX(from, to)).has_value());
    RAWFRAME_EXPECT(transaction->commit().has_value());
}

std::string xOf(const AuthoredScene& scene) {
    const auto& fields = scene.scene().entities[0].components[0].fields;
    return fields.empty() ? "0" : fields[0].value.number;
}

} // namespace

RAWFRAME_TEST(ACommitIsOneEntryAndOneGeneration) {
    const auto kScene = opened();
    RAWFRAME_EXPECT(kScene->generation() == 0 && !kScene->dirty() && !kScene->canUndo());
    auto transaction = kScene->begin(0);
    RAWFRAME_EXPECT(transaction->stage(moveX("1", "2")).has_value() && transaction->stage(moveX("2", "3")).has_value());
    // Staged, not published.
    RAWFRAME_EXPECT(xOf(*kScene) == "1" &&
                    transaction->staged().entities[0].components[0].fields[0].value.number == "3");
    const auto kCommitted = transaction->commit();
    RAWFRAME_EXPECT(kCommitted.has_value() && kCommitted->generation == 1 && kCommitted->deltas == 2);
    RAWFRAME_EXPECT(xOf(*kScene) == "3" && kScene->dirty() && kScene->undoable() == 1);
    // An empty transaction is a no-op: no entry, no generation.
    auto empty = kScene->begin(1);
    const auto kNothing = empty->commit();
    RAWFRAME_EXPECT(kNothing.has_value() && kNothing->generation == 1 && kNothing->deltas == 0 &&
                    kScene->undoable() == 1);
}

RAWFRAME_TEST(FailureAndCancelLeaveNothing) {
    const auto kScene = opened();
    const std::string kBefore = kScene->text();
    {
        // Dropped open: cancelled.
        auto transaction = kScene->begin(0);
        RAWFRAME_EXPECT(transaction->stage(moveX("1", "5")).has_value());
    }
    RAWFRAME_EXPECT(kScene->text() == kBefore && !kScene->inTransaction() && kScene->generation() == 0);
    // A delta that does not apply fails the whole transaction.
    auto failing = kScene->begin(0);
    RAWFRAME_EXPECT(failing->stage(moveX("1", "5")).has_value());
    RAWFRAME_EXPECT(refusedWith(failing->stage(moveX("9", "6")), AuthoringError::DeltaMismatch));
    RAWFRAME_EXPECT(refusedWith(failing->commit(), AuthoringError::Conflict) && kScene->text() == kBefore);
    // A journal that leaves the scene out of form: x at its default of
    // nought written out.
    auto unformed = kScene->begin(0);
    Delta zero = moveX("1", "2");
    zero.after.field = number("0");
    RAWFRAME_EXPECT(unformed->stage(zero).has_value());
    RAWFRAME_EXPECT(refusedWith(unformed->commit(), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(kScene->text() == kBefore && kScene->generation() == 0 && !kScene->canUndo());
    // Past the journal's limits.
    const auto kSmall = opened({.maximumJournalDeltas = 1});
    auto large = kSmall->begin(0);
    RAWFRAME_EXPECT(large->stage(moveX("1", "2")).has_value());
    RAWFRAME_EXPECT(refusedWith(large->stage(moveX("2", "3")), AuthoringError::LimitExceeded));
    RAWFRAME_EXPECT(xOf(*kSmall) == "1" && !kSmall->inTransaction());
}

RAWFRAME_TEST(ATransactionIsBoundToItsGenerationAndJoinsInside) {
    const auto kScene = opened();
    RAWFRAME_EXPECT(refusedWith(kScene->begin(1), AuthoringError::TargetStale));
    auto outer = kScene->begin(0);
    auto inner = kScene->begin(0);
    RAWFRAME_EXPECT(inner->stage(moveX("1", "2")).has_value());
    // The inner token closes its scope; only the outer publishes.
    RAWFRAME_EXPECT(inner->commit().has_value() && xOf(*kScene) == "1");
    RAWFRAME_EXPECT(outer->stage(moveX("2", "3")).has_value());
    const auto kCommitted = outer->commit();
    RAWFRAME_EXPECT(kCommitted.has_value() && kCommitted->deltas == 2 && xOf(*kScene) == "3");
    // An inner cancel discards the whole, and a stale token is inert.
    auto second = kScene->begin(1);
    auto nested = kScene->begin(1);
    RAWFRAME_EXPECT(second->stage(moveX("3", "4")).has_value());
    nested->cancel();
    RAWFRAME_EXPECT(!kScene->inTransaction() && refusedWith(second->commit(), AuthoringError::Conflict));
    auto third = kScene->begin(1);
    RAWFRAME_EXPECT(refusedWith(second->stage(moveX("3", "4")), AuthoringError::Conflict) && kScene->inTransaction());
    third->cancel();
    RAWFRAME_EXPECT(xOf(*kScene) == "3" && kScene->undoable() == 1);
}

RAWFRAME_TEST(UndoAndRedoAreExactAndKeepTheCleanPlace) {
    const auto kScene = opened();
    const std::string kSaved = kScene->text();
    commitMove(*kScene, "1", "2");
    commitMove(*kScene, "2", "3");
    const std::string kLatest = kScene->text();
    RAWFRAME_EXPECT(refusedWith(kScene->undo(1), AuthoringError::TargetStale));
    RAWFRAME_EXPECT(kScene->undo(2).has_value() && xOf(*kScene) == "2" && kScene->generation() == 3);
    RAWFRAME_EXPECT(kScene->undo(3).has_value() && kScene->text() == kSaved && !kScene->dirty());
    RAWFRAME_EXPECT(refusedWith(kScene->undo(4), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(kScene->redo(4).has_value() && kScene->dirty());
    RAWFRAME_EXPECT(kScene->redo(5).has_value() && kScene->text() == kLatest && kScene->redoable() == 0);
    // Saving here, then undoing and redoing back, is clean again.
    kScene->markSaved();
    RAWFRAME_EXPECT(kScene->undo(6).has_value() && kScene->dirty());
    RAWFRAME_EXPECT(kScene->redo(7).has_value() && !kScene->dirty());
    // A commit after an undo drops the redo side, and the clean place with
    // it.
    RAWFRAME_EXPECT(kScene->undo(8).has_value() && kScene->redoable() == 1);
    commitMove(*kScene, "2", "9");
    RAWFRAME_EXPECT(kScene->redoable() == 0 && kScene->dirty() && kScene->undoable() == 2);
    RAWFRAME_EXPECT(kScene->undo(10).has_value() && kScene->dirty());
    // No undo inside a transaction.
    auto open = kScene->begin(11);
    RAWFRAME_EXPECT(refusedWith(kScene->undo(11), AuthoringError::Conflict));
}

RAWFRAME_TEST(TheHistoryIsBoundedAndNeverFalselyClean) {
    const auto kScene = opened({.maximumEntries = 2});
    commitMove(*kScene, "1", "2");
    commitMove(*kScene, "2", "3");
    commitMove(*kScene, "3", "4");
    RAWFRAME_EXPECT(kScene->undoable() == 2);
    // Back as far as it goes: the saved document's entry was evicted, so
    // it is dirty even though no entry is left to undo.
    RAWFRAME_EXPECT(kScene->undo(3).has_value() && kScene->undo(4).has_value() && !kScene->canUndo());
    RAWFRAME_EXPECT(xOf(*kScene) == "2" && kScene->dirty());
    // Bytes bound it too: one entry's bytes more than the budget keeps none.
    const auto kTight = opened({.maximumBytes = 16});
    commitMove(*kTight, "1", "2");
    RAWFRAME_EXPECT(!kTight->canUndo() && kTight->dirty());
}

RAWFRAME_TEST(ACoalescingTransactionKeepsFirstBeforeAndLastAfter) {
    const auto kScene = opened();
    auto drag = kScene->begin(0, Coalescing::FoldSlots);
    for (const auto& [kFrom, kTo] : {std::pair{"1", "2"}, {"2", "3"}, {"3", "4"}}) {
        RAWFRAME_EXPECT(drag->stage(moveX(kFrom, kTo)).has_value());
    }
    const auto kCommitted = drag->commit();
    RAWFRAME_EXPECT(kCommitted.has_value() && kCommitted->deltas == 1 && xOf(*kScene) == "4");
    RAWFRAME_EXPECT(kScene->undo(1).has_value() && xOf(*kScene) == "1");
    // Dragged back where it started: nothing to keep.
    auto back = kScene->begin(2, Coalescing::FoldSlots);
    RAWFRAME_EXPECT(back->stage(moveX("1", "5")).has_value() && back->stage(moveX("5", "1")).has_value());
    const auto kNothing = back->commit();
    RAWFRAME_EXPECT(kNothing.has_value() && kNothing->deltas == 0 && kScene->generation() == 2 &&
                    kScene->redoable() == 1);
}
