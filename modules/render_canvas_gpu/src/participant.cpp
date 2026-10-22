#include "rawframe/composition/composition.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_canvas/frames.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/render_canvas_gpu/renderer.h"
#include "rawframe/render_canvas_gpu/ui.h"
#include "rawframe/ui/frames.h"
#include "rawframe/view/preview.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::render_canvas_gpu {

namespace {

constexpr diagnostics::EventIdentity kDrawingSummary{"canvas", "drawing_summary"};
constexpr diagnostics::EventIdentity kFailed{"canvas", "drawing_failed"};
constexpr diagnostics::EventIdentity kUiDrawingSummary{"ui", "ui_drawing_summary"};
constexpr diagnostics::EventIdentity kUiFailed{"ui", "ui_drawing_failed"};
constexpr std::string_view kMaybe[] = {render::kFrames.name, render_canvas::kCanvasFrames.name};
constexpr std::string_view kMaybeUi[] = {render::kFrames.name, ui::kUiFrames.name, view::kPreviewCamera.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);
/// The canvas's place in a frame: after the scene (SPEC-0024); the UI's,
/// over everything, the scene's post processes over the composed picture
/// too (D376).
constexpr std::uint32_t kOrder = 1;
constexpr std::uint32_t kUiOrder = 3;

/// Records the canvas's frames into the frames `render` makes (D285): in
/// each `present` that plans a frame, the view takes the frame's size and
/// the frame the canvas queued is prepared for it.
class DrawingParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(render::kFrames.name) || !context.has(render_canvas::kCanvasFrames.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(frames_, context.capability(render::kFrames));
        RAWFRAME_TRY_ASSIGN(canvas_, context.capability(render_canvas::kCanvasFrames));
        textures_ = [canvas = canvas_](std::uint64_t id) {
            return canvas->texture(id);
        };
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase /*phase*/, const composition::HostFrame& /*frame*/) noexcept override {
        if (frames_ == nullptr || failed_) {
            return;
        }
        // Joined once the device is ready and there is something to draw,
        // drawing from the next frame planned.
        if (renderer_ == nullptr) {
            render::Device* device = frames_->device();
            if (device == nullptr || canvas_->queued() == nullptr) {
                return;
            }
            auto made = CanvasRenderer::create(*device);
            if (!made.has_value()) {
                failed_ = true;
                emitter_.log(diagnostics::Severity::Error,
                             kFailed,
                             "the canvas could not be drawn: nothing more is",
                             {diagnostics::field("reason", std::string{made.error().description()})});
                return;
            }
            renderer_ = std::move(*made);
            frames_->join(*renderer_, kOrder);
            // In split-screen, a renderer for each other local player's
            // region (D364), each drawing after those before it.
            for (std::size_t other = 1; other < canvas_->regionFrames().size(); ++other) {
                auto another = CanvasRenderer::create(*device);
                if (!another.has_value()) {
                    failed_ = true;
                    emitter_.log(diagnostics::Severity::Error,
                                 kFailed,
                                 "a local player's canvas could not be drawn: nothing more is",
                                 {diagnostics::field("reason", std::string{another.error().description()})});
                    return;
                }
                frames_->join(**another, kOrder);
                others_.push_back(std::move(*another));
            }
        }
        const auto kPlanned = frames_->planned();
        if (!kPlanned.has_value()) {
            return;
        }
        // The view follows the frame from the next one.
        canvas_->resize(kPlanned->first, kPlanned->second);
        const std::span<const render_canvas::CanvasRegion> kRegions = canvas_->regionFrames();
        if (kRegions.empty()) {
            renderer_->prepare(canvas_->queued(), textures_);
        } else {
            const auto kPrepare = [this](CanvasRenderer& renderer, const render_canvas::CanvasRegion& region) {
                const bool kShown = region.width != 0 && region.height != 0;
                renderer.prepare(kShown ? region.frame : nullptr,
                                 textures_,
                                 std::array<std::uint32_t, 4>{region.x, region.y, region.width, region.height},
                                 canvas_->bars());
            };
            kPrepare(*renderer_, kRegions[0]);
            for (std::size_t at = 0; at < others_.size() && at + 1 < kRegions.size(); ++at) {
                kPrepare(*others_[at], kRegions[at + 1]);
                frames_->ready(*others_[at]);
            }
        }
        frames_->ready(*renderer_);
    }

    void stop() noexcept override {
        if (frames_ == nullptr) {
            return;
        }
        RendererStatistics statistics;
        // The other local players' frames drawn (D364).
        std::uint64_t playerFrames = 0;
        for (const std::unique_ptr<CanvasRenderer>& other : others_) {
            playerFrames += other->statistics().frames;
            frames_->leave(*other);
        }
        others_.clear();
        if (renderer_ != nullptr) {
            statistics = renderer_->statistics();
            frames_->leave(*renderer_);
            renderer_.reset();
        }
        emitter_.log(diagnostics::Severity::Info,
                     kDrawingSummary,
                     "what the device drew of one client's canvas",
                     {diagnostics::field("frames", statistics.frames),
                      diagnostics::field("framesWaiting", statistics.framesWaiting),
                      diagnostics::field("draws", statistics.draws),
                      diagnostics::field("drawsLeftOut", statistics.drawsLeftOut),
                      diagnostics::field("texturesUploaded", statistics.texturesUploaded),
                      diagnostics::field("uploadBytes", statistics.uploadBytes),
                      diagnostics::field("uploadsDeferred", statistics.uploadsDeferred),
                      diagnostics::field("texturesReplaced", statistics.texturesReplaced),
                      diagnostics::field("emittersDrawn", statistics.emittersDrawn),
                      diagnostics::field("emittersLeftOut", statistics.emittersLeftOut),
                      diagnostics::field("particlesSpawned", statistics.particlesSpawned),
                      diagnostics::field("ribbonsDrawn", statistics.ribbonsDrawn),
                      diagnostics::field("playerFrames", playerFrames)});
    }

private:
    render::Frames* frames_ = nullptr;
    render_canvas::CanvasFrames* canvas_ = nullptr;
    TextureSource textures_;
    std::unique_ptr<CanvasRenderer> renderer_;
    /// In split-screen, the other local players' renderers (D364); the
    /// first player's is `renderer_`.
    std::vector<std::unique_ptr<CanvasRenderer>> others_;
    bool failed_ = false;
    diagnostics::Emitter emitter_;
};

/// Records the UI's draw list into the frames `render` makes (D376), over
/// everything else: in each `present` that plans a frame, the UI takes the
/// frame's size and what it drew is prepared for it.
class UiDrawingParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(render::kFrames.name) || !context.has(ui::kUiFrames.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(frames_, context.capability(render::kFrames));
        RAWFRAME_TRY_ASSIGN(ui_, context.capability(ui::kUiFrames));
        // An editor's preview, where a client shows one: its view leaves the
        // game's UI out unless it asks for it (D933).
        if (context.has(view::kPreviewCamera.name)) {
            RAWFRAME_TRY_ASSIGN(preview_, context.capability(view::kPreviewCamera));
        }
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase /*phase*/, const composition::HostFrame& /*frame*/) noexcept override {
        if (frames_ == nullptr || failed_) {
            return;
        }
        // Joined once the device is ready and a game has a UI, drawing from
        // the next frame planned.
        if (renderer_ == nullptr) {
            render::Device* device = frames_->device();
            if (device == nullptr || ui_->drawn() == nullptr) {
                return;
            }
            auto made = UiRenderer::create(*device);
            if (!made.has_value()) {
                failed_ = true;
                emitter_.log(diagnostics::Severity::Error,
                             kUiFailed,
                             "the UI could not be drawn: nothing more of it is",
                             {diagnostics::field("reason", std::string{made.error().description()})});
                return;
            }
            renderer_ = std::move(*made);
            frames_->join(*renderer_, kUiOrder);
        }
        const auto kPlanned = frames_->planned();
        if (!kPlanned.has_value()) {
            return;
        }
        // Laid out on the headset's panel where it shows the UI apart
        // (D597), else over the picture.
        const auto kPanel = frames_->plannedPanel();
        const auto kSize = kPanel.value_or(*kPlanned);
        ui_->resize(kSize.first, kSize.second, kPanel.has_value());
        const bool kLeftOut = preview_ != nullptr && preview_->looking().has_value() &&
                              (preview_->overlays() & static_cast<std::uint8_t>(view::Overlay::Ui)) == 0;
        const ui::DrawList* drawn = kLeftOut ? nullptr : ui_->drawn();
        const auto kImages = [ui = ui_](std::uint64_t id) {
            return ui->image(id);
        };
        renderer_->prepare(drawn != nullptr && !drawn->commands.empty() ? drawn : nullptr, kImages);
        prepareAsides(kImages);
        if (same(seen_, drawn)) {
            frames_->unchanged(*renderer_);
        } else {
            frames_->ready(*renderer_);
        }
    }

    void stop() noexcept override {
        if (frames_ == nullptr) {
            return;
        }
        UiStatistics statistics;
        if (renderer_ != nullptr) {
            statistics = renderer_->statistics();
            frames_->leave(*renderer_);
            renderer_.reset();
        }
        for (auto& [kWindow, aside] : asides_) {
            frames_->leave(*aside.renderer);
        }
        asides_.clear();
        emitter_.log(diagnostics::Severity::Info,
                     kUiDrawingSummary,
                     "what the device drew of the local players' UI",
                     {diagnostics::field("frames", statistics.frames),
                      diagnostics::field("framesOnPanel", statistics.framesOnPanel),
                      diagnostics::field("boxes", statistics.boxes),
                      diagnostics::field("images", statistics.images),
                      diagnostics::field("imagesWaiting", statistics.imagesWaiting),
                      diagnostics::field("shadows", statistics.shadows),
                      diagnostics::field("glyphRuns", statistics.glyphRuns),
                      diagnostics::field("glyphs", statistics.glyphs),
                      diagnostics::field("glyphRunsWaiting", statistics.glyphRunsWaiting),
                      diagnostics::field("fieldRuns", statistics.fieldRuns),
                      diagnostics::field("colorRuns", statistics.colorRuns)});
    }

private:
    /// What a renderer's last list drew: the list, whether it drew none,
    /// its atlas's revision, and the images it held.
    struct Seen {
        std::optional<ui::DrawList> last;
        bool drewNone = false;
        std::uint64_t revision = 0;
        std::vector<std::shared_ptr<const texture::Texture>> images;
    };
    /// A window beside the one shown, its renderer and what it last drew
    /// (D988).
    struct Aside {
        std::unique_ptr<UiRenderer> renderer;
        Seen seen;
    };

    /// Each window beside the shown one, where an editor shows a panel
    /// apart (D988): laid out to its size and drawn by its own renderer,
    /// joined as it first shows and left as it closes.
    void prepareAsides(const ImageSource& images) {
        const std::span<const render::FrameTarget::Aside> kPlanned = frames_->plannedAsides();
        std::erase_if(asides_, [&](auto& each) {
            if (std::ranges::contains(kPlanned, each.first, &render::FrameTarget::Aside::window)) {
                return false;
            }
            frames_->leave(*each.second.renderer);
            return true;
        });
        for (const render::FrameTarget::Aside& kShown : kPlanned) {
            ui_->resizeAside(kShown.window, kShown.width, kShown.height);
            const std::span<const ui::UiFrames::Aside> kListed = ui_->asides();
            const auto kFound = std::ranges::find(kListed, kShown.window, &ui::UiFrames::Aside::window);
            const ui::DrawList* drawn = kFound != kListed.end() ? kFound->drawn : nullptr;
            auto at = asides_.find(kShown.window);
            if (at == asides_.end()) {
                auto made = UiRenderer::create(*frames_->device());
                if (!made.has_value()) {
                    continue;
                }
                (*made)->aim(kShown.window);
                frames_->join(**made, kUiOrder);
                at = asides_.emplace(kShown.window, Aside{.renderer = std::move(*made)}).first;
            }
            at->second.renderer->prepare(drawn != nullptr && !drawn->commands.empty() ? drawn : nullptr, images);
            if (same(at->second.seen, drawn)) {
                frames_->unchanged(*at->second.renderer);
            } else {
                frames_->ready(*at->second.renderer);
            }
        }
    }

    /// Whether `drawn` draws what the last list prepared did (D494): the
    /// same commands, glyphs from the same atlas revision, and the same
    /// images held for its keys. Kept as the last, whichever.
    bool same(Seen& seen, const ui::DrawList* drawn) {
        std::vector<std::shared_ptr<const texture::Texture>> images;
        if (drawn != nullptr) {
            for (const ui::Image& each : drawn->images) {
                images.push_back(ui_->image(each.image));
            }
        }
        const std::uint64_t kRevision = drawn != nullptr && drawn->atlas != nullptr ? drawn->atlas->revision : 0;
        // No UI drawn after none is the same frame too, as an editor's
        // view leaving the UI out draws one (D952).
        const bool kSame = drawn == nullptr ? seen.drewNone
                                            : seen.last.has_value() && *drawn == *seen.last &&
                                                  kRevision == seen.revision && images == seen.images;
        seen.drewNone = drawn == nullptr;
        if (drawn == nullptr) {
            seen.last.reset();
        } else if (!kSame) {
            seen.last = *drawn;
        }
        seen.revision = kRevision;
        seen.images = std::move(images);
        return kSame;
    }

    render::Frames* frames_ = nullptr;
    ui::UiFrames* ui_ = nullptr;
    view::PreviewCamera* preview_ = nullptr;
    std::unique_ptr<UiRenderer> renderer_;
    Seen seen_;
    std::map<std::uint64_t, Aside> asides_;
    bool failed_ = false;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makeUi(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<UiDrawingParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<DrawingParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.render_canvas_gpu.drawing",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        // Stopping waits for nothing: the frames' owner waits for the last.
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "render_canvas_gpu.drawing",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.render_canvas_gpu.ui",
        .factory = &makeUi,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kMaybeUi,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "render_canvas_gpu.ui",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
}

} // namespace rawframe::render_canvas_gpu
