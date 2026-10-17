#pragma once

// A session's preview (D433): a running client's tooling endpoint showing
// one of the session's scenes from its view, and, where the game's server
// is named, the presses in it picked against the server (D456), dragged
// (D457, D463), and the mark's handles grabbed and lit (D464 to D468), the
// wheel and right drags moving the view (D469). One preview at a time. The
// session holds the scenes; each call is handed the previewed one, open,
// where it needs it.

#include "handles.h"
#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/session.h"
#include "rawframe/authoring_session/link.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"
#include "viewing.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::authoring_session {

class Preview {
public:
    Preview() = default;
    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;
    ~Preview() {
        letGo();
    }

    /// The scene previewed, empty for none.
    [[nodiscard]] const std::string& scene() const noexcept {
        return scene_;
    }

    /// `preview`'s target reached for `record.scene`, in place of any
    /// preview before; a record with no target only lets the one before go.
    /// Refuses (`capability_denied`) an endpoint that is not loopback, does
    /// not answer, or does not grant `view`, and a server that does not
    /// grant `inspect`.
    [[nodiscard]] result::Status open(const authoring::SessionRecord& record);

    /// Gives the preview's player its camera back, and lets the preview go.
    void letGo();

    /// Hands the preview `scene`'s view where it previews `name` and the
    /// view changed since it was last told; whether the preview is live. A
    /// preview that does not answer is let go.
    bool forward(const std::string& name, const authoring::AuthoredScene& scene);

    /// `pick` (D456): the preview's newest press, if it is new, asked of the
    /// server as a pick; what it moved when let go; the mark's part lit;
    /// and the view the wheel and drags moved, `previewed` being the
    /// previewed scene, open, if it is.
    /// `preview_input` (D790): the events read as tooling input and handed
    /// to the preview of `record.scene`, if it is previewed; refused
    /// (`validation_failed`) where they do not read.
    [[nodiscard]] result::Result<document::Value> inputted(const authoring::SessionRecord& record);
    /// `hide_layers` (D791): the layers handed to the preview of
    /// `record.scene` to leave out, if it is previewed.
    [[nodiscard]] document::Value hid(const authoring::SessionRecord& record);
    /// `view_mode` (D794): the mode handed to the preview of `record.scene`
    /// by its name, if it is previewed; whether it draws so.
    [[nodiscard]] document::Value moded(const authoring::SessionRecord& record);
    /// `overlays` (D798): what the preview of `record.scene` draws over it,
    /// by name, if it is previewed; whether it draws them.
    [[nodiscard]] document::Value overlaid(const authoring::SessionRecord& record);
    /// `drawn_frame` (D842): what the frame the preview of `record.scene`
    /// last drew holds, as its client says, its steps when asked.
    [[nodiscard]] result::Result<document::Value> drawnOf(const authoring::SessionRecord& record);
    /// `split` (D797): the preview of `record.scene` shown in one, two,
    /// or four views, told with its look from now on; whether it splits.
    [[nodiscard]] document::Value split(const authoring::SessionRecord& record, const authoring::AuthoredScene& scene);
    [[nodiscard]] result::Result<document::Value> picked(const authoring::SessionRecord& record,
                                                         authoring::AuthoredScene* previewed);
    /// `run` and `step` (D806): the previewed game's World paused, let
    /// run, or stepped on its server, which grants run; what it says of
    /// itself after. Refused where `record.scene` is not previewed with
    /// its server (`failed_precondition`), and as the server refuses.
    [[nodiscard]] result::Result<document::Value> ran(const authoring::SessionRecord& record);
    /// `traffic` (D807): the previewed game's server's replication totals
    /// and connections, as `tooling.traffic` says them; refused as `ran`.
    [[nodiscard]] result::Result<document::Value> trafficOf(const authoring::SessionRecord& record);
    /// The previewed game's server's profile of its last ticks, as
    /// `tooling.profile` says it, under `authoring.profile` (D838).
    [[nodiscard]] result::Result<document::Value> profileOf(const authoring::SessionRecord& record);
    /// `live_entities` and `live_entity` (D808): the previewed game's World
    /// read on its server, `tooling.entities` and `tooling.read_entity` as
    /// they answer; refused as `ran`, and as the server refuses.
    [[nodiscard]] result::Result<document::Value> live(const authoring::SessionRecord& record);

    /// `mark` (D464): the preview marks a point, or none; whether it shows
    /// it.
    [[nodiscard]] document::Value marked(const authoring::SessionRecord& record);

private:
    /// What the previewed game's server answers to `asked`, under the
    /// session's kind `answered`; refused, saying `what` it lacks.
    [[nodiscard]] result::Result<document::Value> serverSays(const authoring::SessionRecord& record,
                                                             std::string_view asked,
                                                             std::string_view answered,
                                                             std::string_view what);
    /// A press in a preview that met an authored entity or grabbed a
    /// handle of the mark (D457, D466).
    struct Pressed {
        std::int64_t count = 0;
        std::string scene;
        std::string source;
        std::array<double, 3> point{};
        /// The press's ray, along it, and the modifiers held (D463).
        std::array<double, 3> toward{};
        std::int64_t modifiers = 0;
        /// The mark's handle the press grabbed, an axis or the ring (D466,
        /// D467); none for a press that met an entity.
        std::optional<Handle> handle;
        /// The mark the handle was grabbed on, measured from at its release.
        std::array<double, 3> mark{};
    };

    document::Value viewMovedBy(const document::Value& clicked, authoring::AuthoredScene* previewed);
    void lightUnder(const document::Value& clicked);
    [[nodiscard]] double fieldOfView() const noexcept;
    [[nodiscard]] std::optional<Pressed>
    grabbed(std::int64_t count, const document::Value& clicked, const authoring::AuthoredScene* previewed) const;
    [[nodiscard]] document::Value movedAlong(const document::Value& clicked) const;

    /// The preview, and the scene it shows (D433), the view it was last
    /// told, and the looks asked of it.
    std::unique_ptr<ToolingLink> preview_;
    std::string scene_;
    std::optional<authoring::SceneView> previewed_;
    bool told_ = false;
    /// How many views the preview shows, and how many beside its own the
    /// preview last took (D797).
    std::size_t views_ = 1;
    std::int64_t splitShown_ = 0;
    std::uint64_t looks_ = 0;
    /// The previewed game's server, for picking (D456), and the presses
    /// already answered.
    std::unique_ptr<ToolingLink> server_;
    std::int64_t clicks_ = 0;
    /// The newest press, while it is held, when it met an authored entity:
    /// which press, what it met, and where (D457).
    std::optional<Pressed> pressed_;
    /// Where the preview marks, while it shows a mark (D464).
    std::optional<Point> mark_;
    /// The preview's wheel and right-drag totals as last read (D469, D472).
    ViewMotion motionRead_;
    /// The part of it drawn lit: 0 none, 1 to 3 an axis, 4 the ring (D468).
    std::uint8_t lit_ = 0;
};

} // namespace rawframe::authoring_session
