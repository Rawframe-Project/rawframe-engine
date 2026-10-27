// An authoring session's records: each verb's envelope read exactly,
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
    RAWFRAME_EXPECT(kApply.has_value() && !kApply->dryRun);
    // A dry run is told so, true or false.
    const auto kDry = readSessionRecord(R"({"kind":"authoring.apply","scene":"level.scene","dryRun":true,"request":)" +
                                            std::string{kRequest} + "}",
                                        id);
    RAWFRAME_EXPECT(kDry.has_value() && kDry->dryRun && kDry->request.operations.size() == 1);
    const auto kWet = readSessionRecord(R"({"kind":"authoring.apply","scene":"level.scene","dryRun":false,"request":)" +
                                            std::string{kRequest} + "}",
                                        id);
    RAWFRAME_EXPECT(kWet.has_value() && !kWet->dryRun);
    // A drag's step amends its author's latest change, told so by a
    // truth; nothing else amends.
    const auto kAmends = readSessionRecord(
        R"({"kind":"authoring.apply","scene":"level.scene","amends":true,"request":)" + std::string{kRequest} + "}",
        id);
    RAWFRAME_EXPECT(kAmends.has_value() && kAmends->amends && kApply.has_value() && !kApply->amends);
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.apply","scene":"level.scene","amends":1,"request":)" +
                                           std::string{kRequest} + "}",
                                       id)
                         .has_value());
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.mark","scene":"b.scene","at":null,"amends":true})", id).has_value());

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
    // Its handles by name, all unless named.
    RAWFRAME_EXPECT(kMark.has_value() && kMark->handles == 3);
    const auto kRing =
        readSessionRecord(R"({"kind":"authoring.mark","id":14,"scene":"b.scene","at":null,"handles":"ring"})", id);
    RAWFRAME_EXPECT(kRing.has_value() && kRing->handles == 2);
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.mark","scene":"b.scene","at":null,"handles":"arrows"})", id)
             .has_value());
    const auto kPick = readSessionRecord(R"({"kind":"authoring.pick","id":10,"scene":"b.scene"})", id);
    RAWFRAME_EXPECT(kPick.has_value() && kPick->verb == SessionVerb::Pick && kPick->scene == "b.scene");
    // A cast is a ray of three numbers each way, and its scene; a
    // mesh's bounds name the mesh.
    const auto kCast = readSessionRecord(
        R"({"kind":"authoring.cast","id":15,"scene":"b.scene","origin":[0,50,1],"toward":[0,-1050,0]})", id);
    RAWFRAME_EXPECT(kCast.has_value() && kCast->verb == SessionVerb::Cast && kCast->scene == "b.scene" &&
                    kCast->origin[1] == 50 && kCast->toward[1] == -1050);
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.cast","scene":"b.scene","origin":[0,50],"toward":[0,-1,0]})", id)
             .has_value());
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.cast","scene":"b.scene","origin":[0,50,0]})", id).has_value());
    const auto kBounds = readSessionRecord(R"({"kind":"authoring.mesh_bounds","id":16,"asset":"crate.gltf"})", id);
    RAWFRAME_EXPECT(kBounds.has_value() && kBounds->verb == SessionVerb::MeshBounds && kBounds->asset == "crate.gltf");
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.mesh_bounds","id":16})", id).has_value());
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
    // Framing what is chosen names only its scene.
    const auto kFrame = readSessionRecord(R"({"kind":"authoring.frame","id":8,"scene":"b.scene"})", id);
    RAWFRAME_EXPECT(kFrame.has_value() && kFrame->verb == SessionVerb::Frame && kFrame->scene == "b.scene");
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.frame","id":8})", id).has_value());
    // A preview split one, two, or three ways.
    const auto kSplit = readSessionRecord(R"({"kind":"authoring.split","id":10,"scene":"b.scene","views":3})", id);
    RAWFRAME_EXPECT(kSplit.has_value() && kSplit->verb == SessionVerb::Split && kSplit->views == 3);
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.split","id":10,"scene":"b.scene","views":4})", id).has_value());
    // A source's import settings read and set by its path; settings
    // that are no object refused.
    const auto kSettings =
        readSessionRecord(R"({"kind":"authoring.import_settings","id":12,"asset":"paving.png"})", id);
    RAWFRAME_EXPECT(kSettings.has_value() && kSettings->verb == SessionVerb::ImportSettings &&
                    kSettings->asset == "paving.png");
    const auto kSet = readSessionRecord(
        R"({"kind":"authoring.set_import_settings","id":13,"asset":"paving.png","settings":{"exact":false}})", id);
    RAWFRAME_EXPECT(kSet.has_value() && kSet->verb == SessionVerb::SetImportSettings &&
                    kSet->settings.find("exact") != nullptr);
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.set_import_settings","id":13,"asset":"paving.png","settings":[]})", id)
             .has_value());
    // A thumbnail of 16 to 256 pixels.
    const auto kThumbnail =
        readSessionRecord(R"({"kind":"authoring.thumbnail","id":14,"asset":"paving.png","size":64})", id);
    RAWFRAME_EXPECT(kThumbnail.has_value() && kThumbnail->verb == SessionVerb::Thumbnail && kThumbnail->size == 64);
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.thumbnail","id":14,"asset":"paving.png","size":8})", id).has_value());
    // The previewed game paused and stepped, bounded.
    const auto kRun = readSessionRecord(R"({"kind":"authoring.run","id":16,"scene":"gate.scene","paused":true})", id);
    RAWFRAME_EXPECT(kRun.has_value() && kRun->verb == SessionVerb::Run && kRun->paused == std::optional<bool>{true});
    const auto kAsked = readSessionRecord(R"({"kind":"authoring.run","id":17,"scene":"gate.scene"})", id);
    RAWFRAME_EXPECT(kAsked.has_value() && !kAsked->paused.has_value());
    const auto kStep = readSessionRecord(R"({"kind":"authoring.step","id":18,"scene":"gate.scene","ticks":3})", id);
    RAWFRAME_EXPECT(kStep.has_value() && kStep->ticks == 3);
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.step","id":19,"scene":"gate.scene","ticks":601})", id));
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.run","id":20,"scene":"gate.scene","paused":1})", id));
    const auto kTraffic = readSessionRecord(R"({"kind":"authoring.traffic","id":21,"scene":"gate.scene"})", id);
    RAWFRAME_EXPECT(kTraffic.has_value() && kTraffic->verb == SessionVerb::Traffic);
    // The live World's verbs: shapes only.
    const auto kLive = readSessionRecord(
        R"({"kind":"authoring.live_entities","id":22,"scene":"gate.scene","after":"4:1","limit":16})", id);
    RAWFRAME_EXPECT(kLive.has_value() && kLive->verb == SessionVerb::LiveEntities &&
                    kLive->query.find("limit") != nullptr);
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.live_entity","id":23,"scene":"gate.scene","entity":7})", id));
    // A source's references by its path.
    const auto kReferences =
        readSessionRecord(R"({"kind":"authoring.asset_references","id":15,"asset":"crate.png"})", id);
    RAWFRAME_EXPECT(kReferences.has_value() && kReferences->verb == SessionVerb::AssetReferences &&
                    kReferences->asset == "crate.png");
    // Overlays by their names, bounded only.
    const auto kOverlays =
        readSessionRecord(R"({"kind":"authoring.overlays","id":11,"scene":"b.scene","overlays":["colliders"]})", id);
    RAWFRAME_EXPECT(kOverlays.has_value() && kOverlays->verb == SessionVerb::Overlays &&
                    kOverlays->overlays == std::vector<std::string>{"colliders"});
    RAWFRAME_EXPECT(!readSessionRecord(R"({"kind":"authoring.overlays","id":11,"scene":"b.scene","overlays":[""]})", id)
                         .has_value());
    // A view mode by its name, bounded only.
    const auto kMode =
        readSessionRecord(R"({"kind":"authoring.view_mode","id":9,"scene":"b.scene","mode":"unlit"})", id);
    RAWFRAME_EXPECT(kMode.has_value() && kMode->verb == SessionVerb::ViewMode && kMode->mode == "unlit");
    RAWFRAME_EXPECT(
        !readSessionRecord(R"({"kind":"authoring.view_mode","id":9,"scene":"b.scene","mode":""})", id).has_value());
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
    // Only apply runs dry, and only by a boolean.
    RAWFRAME_EXPECT(
        refusedWith(readSessionRecord(R"({"kind":"authoring.apply","scene":"a.scene","dryRun":"yes","request":)" +
                                          std::string{kRequest} + "}",
                                      id),
                    AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readSessionRecord(R"({"kind":"authoring.undo","scene":"a.scene","dryRun":true})", id),
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
    // The multi-document transaction on the wire.
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
    // The long-running operation on the wire.
    document::Value id;
    const auto kCook =
        readSessionRecord(R"({"kind":"authoring.cook","id":"c","output":"/g/content","cache":"C:\\cache"})", id);
    RAWFRAME_EXPECT(kCook.has_value() && kCook->verb == SessionVerb::Cook && kCook->output == "/g/content" &&
                    kCook->cache == "C:\\cache" && id.text() != nullptr && *id.text() == "c");
    const auto kNoCache = readSessionRecord(R"({"kind":"authoring.cook","id":1,"output":"/h/s","cache":null})", id);
    RAWFRAME_EXPECT(kNoCache.has_value() && !kNoCache->cache.has_value());
    // A relative path, a cache left out, and a drive with no separator.
    // A network share is never named: Windows would authenticate to it.
    for (const char* kRefused : {R"({"kind":"authoring.cook","output":"content","cache":null})",
                                 R"({"kind":"authoring.cook","output":"//server/share","cache":null})",
                                 R"({"kind":"authoring.cook","output":"/c","cache":"\\\\server\\share"})",
                                 R"({"kind":"authoring.import","source":"//server/a.png","as":"a.png"})",
                                 R"({"kind":"authoring.cook","output":"/content"})",
                                 R"({"kind":"authoring.cook","output":"/content","cache":"C:cache"})"}) {
        RAWFRAME_EXPECT(refusedWith(readSessionRecord(kRefused, id), AuthoringError::ValidationFailed));
    }
    const auto kCancel = readSessionRecord(R"({"kind":"authoring.cancel","id":2,"operation":"c"})", id);
    RAWFRAME_EXPECT(kCancel.has_value() && kCancel->verb == SessionVerb::Cancel &&
                    kCancel->operation.text() != nullptr && *kCancel->operation.text() == "c");
    RAWFRAME_EXPECT(refusedWith(readSessionRecord(R"({"kind":"authoring.cancel","operation":null})", id),
                                AuthoringError::ValidationFailed));
    // A cook's file read back: an absolute output, a file, and an
    // offset; the session judges the rest.
    const auto kPiece = readSessionRecord(
        R"({"kind":"authoring.cooked_file","id":3,"output":"/content","file":"objects/ab","offset":4})", id);
    RAWFRAME_EXPECT(kPiece.has_value() && kPiece->verb == SessionVerb::CookedFile && kPiece->output == "/content" &&
                    kPiece->file == "objects/ab" && kPiece->offset == 4);
    for (const char* kRefused : {R"({"kind":"authoring.cooked_file","output":"content","file":"a","offset":0})",
                                 R"({"kind":"authoring.cooked_file","output":"/content","file":"","offset":0})",
                                 R"({"kind":"authoring.cooked_file","output":"/content","file":"a","offset":-1})",
                                 R"({"kind":"authoring.cooked_file","output":"/content","file":"a"})"}) {
        RAWFRAME_EXPECT(refusedWith(readSessionRecord(kRefused, id), AuthoringError::ValidationFailed));
    }
    // Stopped as asked is no error.
    RAWFRAME_EXPECT(writeCancelled(document::Value::integer(9), "requested") ==
                    "{\"kind\":\"authoring.reply\",\"id\":9,\"cancelled\":{\"reason\":\"requested\"}}\n");
}

RAWFRAME_TEST(AnImportNamesItsSourceAndWhereItGoes) {
    // A file from outside made one of the game's assets.
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
