// An authoring session's records (D407): each verb's envelope read exactly,
// the request and query documents inside read as on the command line, and
// replies one compact line naming the client's id.

#include "rawframe/authoring/session.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::authoring;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

constexpr std::string_view kRequest =
    R"({"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.create_entity","entity":"0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1","name":"door","place":0}]})";

} // namespace

RAWFRAME_TEST(EachVerbsRecordIsReadWithItsMembers) {
    document::Value id;
    const auto kHello = readSessionRecord(R"({"kind":"authoring.hello","id":"a","surfaceGeneration":1})", id);
    RAWFRAME_EXPECT(kHello.has_value() && kHello->verb == SessionVerb::Hello && kHello->surfaceGeneration == 1);
    RAWFRAME_EXPECT(id.text() != nullptr && *id.text() == "a");

    const auto kApply = readSessionRecord(
        R"({"kind":"authoring.apply","id":2,"scene":"level.scene","request":)" + std::string{kRequest} + "}", id);
    RAWFRAME_EXPECT(kApply.has_value() && kApply->verb == SessionVerb::Apply && kApply->scene == "level.scene");
    RAWFRAME_EXPECT(kApply.has_value() && kApply->request.operations.size() == 1 && id.integer() == 2);

    const auto kRead = readSessionRecord(
        R"({"kind":"authoring.read","scene":"a.scene","queries":{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"scene.list_entities"}]}})",
        id);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->queries.size() == 1 && id.isNull());

    const auto kUndo =
        readSessionRecord(R"({"kind":"authoring.undo","id":4,"scene":"a.scene","expects":"sha256:00"})", id);
    RAWFRAME_EXPECT(kUndo.has_value() && kUndo->verb == SessionVerb::Undo && kUndo->expects == "sha256:00");
    const auto kRedo = readSessionRecord(R"({"kind":"authoring.redo","scene":"a.scene"})", id);
    RAWFRAME_EXPECT(kRedo.has_value() && kRedo->verb == SessionVerb::Redo && !kRedo->expects.has_value());
    const auto kSelect = readSessionRecord(
        R"({"kind":"authoring.select","scene":"a.scene","entities":["00000000-0000-0000-0000-000000000001"]})", id);
    RAWFRAME_EXPECT(kSelect.has_value() && kSelect->verb == SessionVerb::Select && kSelect->entities.size() == 1 &&
                    kSelect->entities[0] == (base::Bits128{0, 1}));
    RAWFRAME_EXPECT(
        readSessionRecord(R"({"kind":"authoring.select","scene":"a.scene","entities":[]})", id)->entities.empty());
    RAWFRAME_EXPECT(readSessionRecord(R"({"kind":"authoring.describe"})", id).has_value());
    RAWFRAME_EXPECT(readSessionRecord(R"({"kind":"authoring.end","id":null})", id).has_value());
}

RAWFRAME_TEST(ARecordOutOfItsFormIsRefusedNamingItsId) {
    document::Value id;
    RAWFRAME_EXPECT(refusedWith(readSessionRecord("not json", id), AuthoringError::ValidationFailed) && id.isNull());
    RAWFRAME_EXPECT(
        refusedWith(readSessionRecord(R"({"kind":"authoring.jump","id":7})", id), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(id.integer() == 7);
    // A member its verb does not have, one it lacks, and a scene not named.
    RAWFRAME_EXPECT(refusedWith(readSessionRecord(R"({"kind":"authoring.describe","scene":"a.scene"})", id),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(readSessionRecord(R"({"kind":"authoring.hello"})", id), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readSessionRecord(R"({"kind":"authoring.apply","scene":"a.scene","queries":{}})", id),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readSessionRecord(R"({"kind":"authoring.undo","scene":""})", id),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(readSessionRecord(R"({"kind":"authoring.apply","scene":"a.scene","expects":"x","request":{}})", id),
                    AuthoringError::ValidationFailed));
    // A selection names entities by their ids, in an array.
    RAWFRAME_EXPECT(refusedWith(readSessionRecord(R"({"kind":"authoring.select","scene":"a.scene"})", id),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(readSessionRecord(R"({"kind":"authoring.select","scene":"a.scene","entities":["spawn"]})", id),
                    AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(readSessionRecord(R"({"kind":"authoring.select","scene":"a.scene","entities":"x"})", id),
                    AuthoringError::ValidationFailed));
    // The request inside is read as the command line reads one.
    RAWFRAME_EXPECT(refusedWith(
        readSessionRecord(
            R"({"kind":"authoring.apply","scene":"a.scene","request":{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.fly"}]}})",
            id),
        AuthoringError::UnsupportedOperation));
}

RAWFRAME_TEST(RepliesAreOneCompactLine) {
    document::Value answer = document::Value::object();
    answer.add("kind", document::Value::string("authoring.ended"));
    RAWFRAME_EXPECT(writeReply(document::Value::integer(3), std::move(answer)) ==
                    "{\"kind\":\"authoring.reply\",\"id\":3,\"answer\":{\"kind\":\"authoring.ended\"}}\n");
    const std::string kRefusal = writeRefusal(
        document::Value{},
        result::fail(
            result::ErrorClass::NotFound, kAuthoringDomain, code(AuthoringError::TargetNotFound), "no such scene")
            .error());
    RAWFRAME_EXPECT(
        kRefusal.starts_with("{\"kind\":\"authoring.reply\",\"id\":null,\"error\":{\"code\":\"target_not_found\""));
    RAWFRAME_EXPECT(kRefusal.find('\n') == kRefusal.size() - 1);
}
