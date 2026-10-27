#pragma once

// A session's preview: a running client's tooling endpoint showing
// one of the session's scenes from its view, and, where the game's server
// is named, the presses in it picked against the server, dragged,
// and the mark's handles grabbed and lit, the
// wheel and right drags moving the view. One preview at a time. The
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
#include <utility>
#include <vector>

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

    /// Hands the preview the scene's `view` where it previews `name` and
    /// the view changed since it was last told, and the paths its splines
    /// make where they changed; whether the preview is live. A
    /// preview that does not answer is let go. Only the view, not the
    /// scene: a preview held beside a host joined has no document.
    bool forward(const std::string& name,
                 const std::optional<authoring::SceneView>& view,
                 std::vector<std::vector<std::array<double, 3>>> paths);

    /// `pick`: the preview's newest press, if it is new, asked of the
    /// server as a pick; what it moved when let go; the mark's part lit;
    /// and the view the wheel and drags moved, `previewed` being the
    /// previewed scene, open, if it is.
    /// `preview_input`: the events read as tooling input and handed
    /// to the preview of `record.scene`, if it is previewed; refused
    /// (`validation_failed`) where they do not read.
    [[nodiscard]] result::Result<document::Value> inputted(const authoring::SessionRecord& record);
    /// `hide_layers`: the layers handed to the preview of
    /// `record.scene` to leave out, if it is previewed.
    [[nodiscard]] document::Value hid(const authoring::SessionRecord& record);
    /// `view_mode`: the mode handed to the preview of `record.scene`
    /// by its name, if it is previewed; whether it draws so.
    [[nodiscard]] document::Value moded(const authoring::SessionRecord& record);
    /// `overlays`: what the preview of `record.scene` draws over it,
    /// by name, if it is previewed; whether it draws them.
    [[nodiscard]] document::Value overlaid(const authoring::SessionRecord& record);
    /// `drawn_frame`: what the frame the preview of `record.scene`
    /// last drew holds, as its client says, its steps when asked.
    [[nodiscard]] result::Result<document::Value> drawnOf(const authoring::SessionRecord& record);
    /// `split`: the preview of `record.scene` shown in one, two,
    /// or four views, told with its look from now on; whether it splits.
    [[nodiscard]] document::Value split(const authoring::SessionRecord& record,
                                        const std::optional<authoring::SceneView>& view);
    [[nodiscard]] result::Result<document::Value> picked(const authoring::SessionRecord& record,
                                                         authoring::AuthoredScene* previewed);
    /// `cast`: what the ray of `record` meets in the previewed
    /// game's World, as its server's `tooling.pick` answers, an authored
    /// entity by its scene and id; refused as `ran`.
    [[nodiscard]] result::Result<document::Value> cast(const authoring::SessionRecord& record);
    /// `run` and `step`: the previewed game's World paused, let
    /// run, or stepped on its server, which grants run; what it says of
    /// itself after. Refused where `record.scene` is not previewed with
    /// its server (`failed_precondition`), and as the server refuses.
    [[nodiscard]] result::Result<document::Value> ran(const authoring::SessionRecord& record);
    /// `traffic`: the previewed game's server's replication totals
    /// and connections, as `tooling.traffic` says them; refused as `ran`.
    [[nodiscard]] result::Result<document::Value> trafficOf(const authoring::SessionRecord& record);
    /// The previewed game's server's profile of its last ticks, as
    /// `tooling.profile` says it, under `authoring.profile`.
    [[nodiscard]] result::Result<document::Value> profileOf(const authoring::SessionRecord& record);
    /// `live_entities` and `live_entity`: the previewed game's World
    /// read on its server, `tooling.entities` and `tooling.read_entity` as
    /// they answer; refused as `ran`, and as the server refuses.
    [[nodiscard]] result::Result<document::Value> live(const authoring::SessionRecord& record);

    /// `mark`: the preview marks a point, or none; whether it shows
    /// it.
    [[nodiscard]] document::Value marked(const authoring::SessionRecord& record);
    /// `outline`: `outlines`, as `outlinesOf` makes them, handed to
    /// the preview of `scene`, if it is previewed; whether it draws them.
    bool outline(std::string_view scene, const document::Value& outlines);

private:
    /// What the previewed game's server answers to `asked`, under the
    /// session's kind `answered`; refused, saying `what` it lacks.
    [[nodiscard]] result::Result<document::Value> serverSays(const authoring::SessionRecord& record,
                                                             std::string_view asked,
                                                             std::string_view answered,
                                                             std::string_view what);
    /// A press in a preview that met an authored entity or grabbed a
    /// handle of the mark.
    struct Pressed {
        std::int64_t count = 0;
        std::string scene;
        std::string source;
        std::array<double, 3> point{};
        /// The press's ray, along it, and the modifiers held.
        std::array<double, 3> toward{};
        std::int64_t modifiers = 0;
        /// The mark's handle the press grabbed, an axis or the ring;
        /// none for a press that met an entity.
        std::optional<Handle> handle;
        /// The mark the handle was grabbed on, measured from at its release.
        std::array<double, 3> mark{};
    };

    document::Value viewMovedBy(const document::Value& clicked, authoring::AuthoredScene* previewed);
    void lightUnder(const document::Value& clicked);
    [[nodiscard]] double fieldOfView() const noexcept;
    [[nodiscard]] std::optional<Pressed>
    grabbed(std::int64_t count, const document::Value& clicked, const authoring::AuthoredScene* previewed) const;
    /// Where the press held would move what it met were it let go along
    /// a ray: null where the ray meets no plane.
    [[nodiscard]] document::Value movedTo(const std::optional<Point>& origin, const std::optional<Point>& toward) const;
    /// The preview's answers to what was sent it without waiting:
    /// its newest press, and whether the last input it answered was taken.
    void hear();

    /// The preview, and the scene it shows, the view it was last
    /// told, and the looks asked of it.
    std::unique_ptr<ToolingLink> preview_;
    std::string scene_;
    /// Whether the game is only observed, its own camera kept.
    bool observe_ = false;
    std::optional<authoring::SceneView> previewed_;
    bool told_ = false;
    /// The paths it was last told to draw, none before.
    std::optional<std::vector<std::vector<std::array<double, 3>>>> pathsTold_;
    /// How many views the preview shows, and how many beside its own the
    /// preview last took.
    std::size_t views_ = 1;
    std::int64_t splitShown_ = 0;
    std::uint64_t looks_ = 0;
    /// The previewed game's server, for picking, and the presses
    /// already answered.
    std::unique_ptr<ToolingLink> server_;
    std::int64_t clicks_ = 0;
    /// Asked of the preview without waiting, as its frames come: a
    /// Studio that waited for each answer was held to the preview's pace,
    /// a third of its time on a slow one. Its presses, asked once at a
    /// time, the newest answer heard; and the inputs not yet answered, at
    /// most kMostInputsWaiting, and whether the last answered was taken.
    std::optional<document::Value> clickedHeard_;
    bool clickedAsked_ = false;
    std::size_t inputsWaiting_ = 0;
    bool inputTaken_ = false;
    static constexpr std::size_t kMostInputsWaiting = 64;
    /// The newest press, while it is held, when it met an authored entity:
    /// which press, what it met, and where.
    std::optional<Pressed> pressed_;
    /// Where the preview marks, while it shows a mark.
    std::optional<Point> mark_;
    /// The preview's wheel and right-drag totals as last read.
    ViewMotion motionRead_;
    /// How many times the preview had flown its own camera when last read.
    std::int64_t flightsRead_ = 0;
    /// The part of it drawn lit: 0 none, 1 to 3 an axis, 4 the ring.
    std::uint8_t lit_ = 0;
    /// The newest press and the way its ray went, until it is let go: a
    /// box drawn from it to where it is let go chooses what it holds.
    std::optional<std::pair<std::int64_t, Point>> boxFrom_;
    /// Its handles shown and grabbed, as the author's tool asks.
    std::uint8_t handles_ = kAllHandles;
};

} // namespace rawframe::authoring_session
