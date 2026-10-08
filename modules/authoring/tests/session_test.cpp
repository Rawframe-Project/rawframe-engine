// An authoring session's records (D407): each verb's envelope read exactly,
// the request and query documents inside read as on the command line, and
// replies one compact line naming the client's id.

#include "rawframe/authoring/session.h"
#include "rawframe/test/test.h"

#include <string>
#include <string_view>
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
    const auto kAssets = readSessionRecord(R"({"kind":"authoring.assets","id":9})", id);
    RAWFRAME_EXPECT(kAssets.has_value() && kAssets->verb == SessionVerb::Assets);
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.assets","id":9,"scene":"b.scene"})", id).has_value());
    const auto kMark = readSessionRecord(R"({"kind":"authoring.mark","id":12,"scene":"b.scene","at":[1,2.5,-3]})", id);
    RAWFRAME_EXPECT(kMark.has_value() && kMark->verb == SessionVerb::Mark && kMark->mark.has_value() &&
                    (*kMark->mark)[1] == 2.5);
    const auto kUnmarked = readSessionRecord(R"({"kind":"authoring.mark","id":13,"scene":"b.scene","at":null})", id);
    RAWFRAME_EXPECT(kUnmarked.has_value() && !kUnmarked->mark.has_value());
    // A mark is a point of three numbers, or null, and nothing else.
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.mark","scene":"b.scene","at":[1,2]})", id).has_value());
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.mark","scene":"b.scene"})", id).has_value());
    const auto kPick = readSessionRecord(R"({"kind":"authoring.pick","id":10,"scene":"b.scene"})", id);
    RAWFRAME_EXPECT(kPick.has_value() && kPick->verb == SessionVerb::Pick && kPick->scene == "b.scene");
    const auto kServed = readSessionRecord(
        R"({"kind":"authoring.preview","id":11,"scene":"b.scene","preview":{"endpoint":"127.0.0.1:2","pinFile":"c",)"
        R"("tokenFile":"t","server":{"endpoint":"127.0.0.1:3","pinFile":"s"}}})",
        id);
    RAWFRAME_EXPECT(kServed.has_value() && kServed->preview->serverEndpoint == "127.0.0.1:3" &&
                    kServed->preview->serverPinFile == "s");
    // A server is an endpoint and a pin file, both.
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.preview","scene":"b.scene","preview":{"endpoint":"127.0.0.1:2",)"
                           R"("pinFile":"c","tokenFile":"t","server":{"endpoint":"127.0.0.1:3"}}})",
                           id)
             .has_value());
    const auto kHistory = readSessionRecord(R"({"kind":"authoring.history","id":7,"scene":"b.scene"})", id);
    RAWFRAME_EXPECT(kHistory.has_value() && kHistory->verb == SessionVerb::History && kHistory->scene == "b.scene");
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.history","id":8})", id).has_value());
    const auto kCreate = readSessionRecord(R"({"kind":"authoring.create_scene","id":5,"scene":"b.scene"})", id);
    RAWFRAME_EXPECT(kCreate.has_value() && kCreate->verb == SessionVerb::CreateScene && kCreate->scene == "b.scene");
    // A new scene is named by its path and nothing else.
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.create_scene","id":6})", id).has_value());
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.create_scene","scene":"b.scene","expects":"sha256:00"})", id)
             .has_value());
    const auto kRedo = readSessionRecord(R"({"kind":"authoring.redo","scene":"a.scene"})", id);
    RAWFRAME_EXPECT(kRedo.has_value() && kRedo->verb == SessionVerb::Redo && !kRedo->expects.has_value());
    const auto kSelect = readSessionRecord(
        R"({"kind":"authoring.select","scene":"a.scene","entities":["00000000-0000-0000-0000-000000000001"]})", id);
    RAWFRAME_EXPECT(kSelect.has_value() && kSelect->verb == SessionVerb::Select && kSelect->entities.size() == 1 &&
                    kSelect->entities[0] == (base::Bits128{0, 1}));
    RAWFRAME_EXPECT(
        readSessionRecord(R"({"kind":"authoring.select","scene":"a.scene","entities":[]})", id)->entities.empty());
    const auto kView = readSessionRecord(
        R"({"kind":"authoring.view","scene":"a.scene","view":{"eye":[0,5,10],"target":[0,0,0.5],"fieldOfView":60}})",
        id);
    RAWFRAME_EXPECT(kView.has_value() && kView->verb == SessionVerb::View && kView->view.target[2] == 0.5 &&
                    kView->view.fieldOfView == 60);
    for (
        const std::string_view kWrong :
        {R"({"kind":"authoring.view","scene":"a.scene","view":{"eye":[0,5],"target":[0,0,0],"fieldOfView":60}})",
         R"({"kind":"authoring.view","scene":"a.scene","view":{"eye":[0,5,1],"target":[0,0,0]}})",
         R"({"kind":"authoring.view","scene":"a.scene","view":{"eye":[0,5,1],"target":[0,0,0],"fieldOfView":60,"roll":1}})",
         R"({"kind":"authoring.view","scene":"a.scene"})"}) {
        RAWFRAME_EXPECT(refusedWith(readSessionRecord(kWrong, id), AuthoringError::ValidationFailed));
    }
    const auto kPreview = readSessionRecord(
        R"({"kind":"authoring.preview","scene":"a.scene","preview":{"endpoint":"127.0.0.1:9000","pinFile":"pin","tokenFile":"token"}})",
        id);
    RAWFRAME_EXPECT(kPreview.has_value() && kPreview->verb == SessionVerb::Preview && kPreview->preview.has_value() &&
                    kPreview->preview->endpoint == "127.0.0.1:9000" && kPreview->preview->tokenFile == "token");
    const auto kLetGo = readSessionRecord(R"({"kind":"authoring.preview","scene":"a.scene","preview":null})", id);
    RAWFRAME_EXPECT(kLetGo.has_value() && !kLetGo->preview.has_value());
    for (const std::string_view kWrong :
         {R"({"kind":"authoring.preview","scene":"a.scene","preview":{"endpoint":"127.0.0.1:9000","pinFile":"pin"}})",
          R"({"kind":"authoring.preview","scene":"a.scene","preview":{"endpoint":"","pinFile":"pin","tokenFile":"t"}})",
          R"({"kind":"authoring.preview","scene":"a.scene","preview":"127.0.0.1:9000"})",
          R"({"kind":"authoring.preview","preview":null})"}) {
        RAWFRAME_EXPECT(refusedWith(readSessionRecord(kWrong, id), AuthoringError::ValidationFailed));
    }
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

RAWFRAME_TEST(ApplyTogetherReadsTwoOrMoreScenesEachOnce) {
    // SPEC-0040's multi-document transaction on the wire (D497).
    document::Value id;
    const std::string kPart = R"({"scene":"level.scene","request":)" + std::string{kRequest} + "}";
    const std::string kOther = R"({"scene":"hall.scene","request":)" + std::string{kRequest} + "}";
    const auto kTogether = readSessionRecord(
        R"({"kind":"authoring.apply_together","id":3,"documents":[)" + kPart + "," + kOther + "]}", id);
    RAWFRAME_EXPECT(kTogether.has_value() && kTogether->verb == SessionVerb::ApplyTogether &&
                    kTogether->parts.size() == 2 && kTogether->parts[1].scene == "hall.scene" &&
                    kTogether->parts[0].request.operations.size() == 1 && id.integer() == 3);
    // One document, a scene twice, a request not atomic, and a part with
    // more than its scene and request.
    RAWFRAME_EXPECT(
        refusedWith(readSessionRecord(R"({"kind":"authoring.apply_together","documents":[)" + kPart + "]}", id),
                    AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(
        readSessionRecord(R"({"kind":"authoring.apply_together","documents":[)" + kPart + "," + kPart + "]}", id),
        AuthoringError::ValidationFailed));
    std::string independent = kOther;
    independent.replace(independent.find("\"atomic\""), 8, "\"halt_remaining\"");
    RAWFRAME_EXPECT(refusedWith(
        readSessionRecord(R"({"kind":"authoring.apply_together","documents":[)" + kPart + "," + independent + "]}", id),
        AuthoringError::ValidationFailed));
    std::string extra = kOther;
    extra.insert(1, R"("expects":"x",)");
    RAWFRAME_EXPECT(refusedWith(
        readSessionRecord(R"({"kind":"authoring.apply_together","documents":[)" + kPart + "," + extra + "]}", id),
        AuthoringError::ValidationFailed));
}

RAWFRAME_TEST(ACookNamesWhereItCooksToAndACancelItsOperation) {
    // SPEC-0040's long-running operation on the wire (D502).
    document::Value id;
    const auto kCook =
        readSessionRecord(R"({"kind":"authoring.cook","id":"c","output":"/g/content","cache":"C:\\cache"})", id);
    RAWFRAME_EXPECT(kCook.has_value() && kCook->verb == SessionVerb::Cook && kCook->output == "/g/content" &&
                    kCook->cache == "C:\\cache" && id.text() != nullptr && *id.text() == "c");
    const auto kNoCache = readSessionRecord(R"({"kind":"authoring.cook","id":1,"output":"//h/s","cache":null})", id);
    RAWFRAME_EXPECT(kNoCache.has_value() && !kNoCache->cache.has_value());
    // A relative path, a cache left out, and a drive with no separator.
    for (const char* kRefused : {R"({"kind":"authoring.cook","output":"content","cache":null})",
                                 R"({"kind":"authoring.cook","output":"/content"})",
                                 R"({"kind":"authoring.cook","output":"/content","cache":"C:cache"})"}) {
        RAWFRAME_EXPECT(refusedWith(readSessionRecord(kRefused, id), AuthoringError::ValidationFailed));
    }
    const auto kCancel = readSessionRecord(R"({"kind":"authoring.cancel","id":2,"operation":"c"})", id);
    RAWFRAME_EXPECT(kCancel.has_value() && kCancel->verb == SessionVerb::Cancel &&
                    kCancel->operation.text() != nullptr && *kCancel->operation.text() == "c");
    RAWFRAME_EXPECT(refusedWith(readSessionRecord(R"({"kind":"authoring.cancel","operation":null})", id),
                                AuthoringError::ValidationFailed));
    // Stopped as asked is no error.
    RAWFRAME_EXPECT(writeCancelled(document::Value::integer(9), "requested") ==
                    "{\"kind\":\"authoring.reply\",\"id\":9,\"cancelled\":{\"reason\":\"requested\"}}\n");
}

RAWFRAME_TEST(AnImportNamesItsSourceAndWhereItGoes) {
    // D503: a file from outside made one of the game's assets.
    document::Value id;
    const auto kImport =
        readSessionRecord(R"({"kind":"authoring.import","id":7,"source":"/art/bark.png","as":"art/bark.png"})", id);
    RAWFRAME_EXPECT(kImport.has_value() && kImport->verb == SessionVerb::Import && kImport->source == "/art/bark.png" &&
                    kImport->as == "art/bark.png");
    for (const char* kRefused : {R"({"kind":"authoring.import","source":"art/bark.png","as":"bark.png"})",
                                 R"({"kind":"authoring.import","source":"/art/bark.png","as":""})",
                                 R"({"kind":"authoring.import","source":"/art/bark.png"})"}) {
        RAWFRAME_EXPECT(refusedWith(readSessionRecord(kRefused, id), AuthoringError::ValidationFailed));
    }
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
