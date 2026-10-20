#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/content/errors.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/game_textures/game_textures.h"
#include "rawframe/material/canvas.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/render_canvas/canvas.h"
#include "rawframe/render_canvas/errors.h"
#include "rawframe/render_canvas/frames.h"
#include "rawframe/render_canvas/registrar.h"
#include "rawframe/view/players.h"
#include "rawframe/view/preview.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::render_canvas {

namespace {

/// The grid an editor's 2D view shows over the canvas (D933): a line a
/// meter, brighter every ten, ten meters a line where the view is taller
/// than sixty, out past what the view sees; faint, so what is drawn reads
/// through it, the X axis red and the Y axis green where they pass, within
/// half the lines a canvas draws.
std::vector<CanvasLine> gridLinesAbout(const CanvasCamera& camera) {
    std::vector<CanvasLine> made;
    const double kTall = std::max(1.0, static_cast<double>(camera.height));
    const double kWide = kTall * std::max(0.1, static_cast<double>(camera.aspect));
    double step = kTall > 60 ? 10 : 1;
    while ((kTall + kWide) / step > static_cast<double>(kMostCanvasLines / 2) - 4) {
        step *= 10;
    }
    const double kLeft = std::floor((camera.x - kWide) / step) * step;
    const double kRight = camera.x + kWide;
    const double kBottom = std::floor((camera.y - kTall) / step) * step;
    const double kTop = camera.y + kTall;
    const auto kColor = [step](double place, std::uint32_t axis) {
        if (std::abs(place) < step / 2) {
            return axis;
        }
        return std::abs(std::remainder(place, step * 10)) < step / 2 ? 0x8A8F9A70U : 0x6A6E7840U;
    };
    for (double x = kLeft; x <= kRight; x += step) {
        made.push_back(CanvasLine{.from = {x, kBottom}, .to = {x, kTop}, .color = kColor(x, 0x4CB86AC0U)});
    }
    for (double y = kBottom; y <= kTop; y += step) {
        made.push_back(CanvasLine{.from = {kLeft, y}, .to = {kRight, y}, .color = kColor(y, 0xD94D4DC0U)});
    }
    return made;
}

constexpr diagnostics::EventIdentity kCanvasSummary{"canvas", "canvas_summary"};
constexpr diagnostics::EventIdentity kUnread{"canvas", "textures_unavailable"};
constexpr diagnostics::EventIdentity kUnreadTexture{"canvas", "texture_unavailable"};
constexpr diagnostics::EventIdentity kTextureReloaded{"canvas", "texture_reloaded"};
constexpr diagnostics::EventIdentity kTextureNotReloaded{"canvas", "texture_reload_failed"};
constexpr diagnostics::EventIdentity kTexturesRead{"canvas", "textures_read"};
constexpr diagnostics::EventIdentity kUnreadMaterial{"canvas", "material_unavailable"};
constexpr std::string_view kProvided[] = {kCanvasFrames.name};
constexpr std::string_view kMaybe[] = {world_replication::kClientWorlds.name,
                                       world_kest::kGameFiles.name,
                                       game_content::kGameContent.name,
                                       view::kPlayerViews.name,
                                       view::kPreviewCamera.name};
/// The decoded levels the canvas holds at most.
/// A client's view without a camera of its own: 10 meters tall.
constexpr float kDefaultViewHeight = 10;
constexpr std::uint64_t kTextureBudgetBytes = std::uint64_t{256} * 1024 * 1024;

/// A texture's identity as its game writes it, 16 hexadecimal digits.
std::string identityText(std::uint64_t id) {
    std::string text(16, '0');
    for (std::size_t at = 0; at < 16; ++at) {
        text[15 - at] = "0123456789abcdef"[(id >> (4 * at)) & 0xFU];
    }
    return text;
}

/// Whether a read found a resource of another type.
bool otherType(const result::Error& error) {
    return error.domain() == content::kContentDomain &&
           error.code() == content::code(content::ContentError::ResourceTypeMismatch);
}

/// A canvas material's cooked bytes, read and waited for, and decoded
/// (D356).
result::Result<material::CanvasMaterial> readCanvasMaterial(content::ContentStore& store, base::Bits128 id) {
    RAWFRAME_TRY_ASSIGN(
        execution::AsyncHandle<content::VerifiedContent> read,
        store.read(content::ResourceRef{.id = content::ResourceId{id},
                                        .type = content::ResourceTypeId{material::kCanvasMaterialType}}));
    RAWFRAME_TRY_ASSIGN(
        const content::VerifiedContent kRead,
        execution::toResult(read.wait(),
                            execution::CancellationMapping{.errorClass = result::ErrorClass::Unavailable,
                                                           .domain = kRenderCanvasDomain,
                                                           .code = code(RenderCanvasError::MaterialUnreadable),
                                                           .description = "a canvas material's read was cancelled"}));
    return material::decodeCanvas(kRead.bytes());
}

/// Draws one client's mirrored World each frame through a camera following
/// its player: the extract stage in `presentation_extract`, the queue stage
/// in `present`. Idle without a game that has sprites or without clients.
/// It reads the game's textures from the Runtime's cooked content
/// (`rawframe.content.game`) when the process has some; without it, it
/// draws on, every draw waiting for its texture. The frame it queued is
/// lent to a device's recording (`rawframe.render_canvas.frames`).
class CanvasParticipant final : public composition::Participant, public CanvasFrames {
public:
    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(world_kest::kGameFiles.name) || !context.has(world_replication::kClientWorlds.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(const world_kest::GameFiles* files, context.capability(world_kest::kGameFiles));
        if (!files->named()) {
            return {};
        }
        std::string report;
        auto program = files->compile(files->description().program, {}, &report);
        if (!program.has_value()) {
            return std::unexpected<result::Error>{std::move(program)
                                                      .error()
                                                      .withContext("program", files->description().program)
                                                      .withContext("report", report)};
        }
        auto game = loadGameCanvas(*files, **program);
        if (!game.has_value()) {
            // A game without sprites draws nothing, and needs nothing drawn.
            if (game.error().domain() == kRenderCanvasDomain &&
                game.error().code() == code(RenderCanvasError::NoSprites)) {
                return {};
            }
            return std::unexpected<result::Error>{std::move(game).error()};
        }
        const composition::Configuration& configuration = context.configuration();
        RAWFRAME_TRY_ASSIGN(clients_, context.capability(world_replication::kClientWorlds));
        if (configuration.text("canvas.client").has_value()) {
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kClient, configuration.unsignedInteger("canvas.client", 0));
            client_ = static_cast<std::size_t>(kClient);
        } else if (context.has(view::kPlayerViews.name)) {
            // The local players' views, told where the host lends them
            // (D367); a canvas of a client named by configuration is no
            // player's.
            RAWFRAME_TRY_ASSIGN(views_, context.capability(view::kPlayerViews));
        }
        // A preview's camera, where an authoring client sets one (D793).
        if (!client_.has_value() && context.has(view::kPreviewCamera.name)) {
            RAWFRAME_TRY_ASSIGN(preview_, context.capability(view::kPreviewCamera));
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kWidth, configuration.unsignedInteger("canvas.width", 1280));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kHeight, configuration.unsignedInteger("canvas.height", 720));
        if (kWidth == 0 || kHeight == 0 || kWidth > 1U << 16U || kHeight > 1U << 16U) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "canvas.width and canvas.height are 1 to 65536 pixels")
                                                      .error()};
        }
        // Split-screen (D364): the process's local players, each in its
        // region by the game's layout for their count, the first in the
        // first, as the scene's are.
        if (const std::size_t kPlayers = clients_->localPlayers(); kPlayers > 1) {
            const auto& kLayouts = files->description().layouts;
            const auto kLayout = std::ranges::find(kLayouts, kPlayers, &world_kest::GameLayout::players);
            if (client_.has_value() || kLayout == kLayouts.end()) {
                return std::unexpected<result::Error>{
                    result::fail(result::ErrorClass::InvalidArgument,
                                 composition::kCompositionDomain,
                                 code(composition::CompositionError::BadConfiguration),
                                 "local players are shown by the game's layout for their count, which it must have, "
                                 "and canvas.client names none")
                        .error()};
            }
            client_ = 0;
            regions_ = kLayout->regions;
            for (std::size_t other = 1; other < kPlayers; ++other) {
                localPlayers_.push_back(LocalPlayer{.client = other});
            }
            regionFrames_.resize(regions_.size());
        }
        // A constrained aspect (D369): one player's view is placed in the
        // window as a split-screen player's is in its region.
        aspect_ = files->description().aspect;
        if (aspect_.has_value() && regions_.empty()) {
            regions_.push_back(world_kest::GameRegion{});
            regionFrames_.resize(1);
        }
        cameraComponent_ = game->camera;
        camera_.aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
        width_ = static_cast<std::uint32_t>(kWidth);
        height_ = static_cast<std::uint32_t>(kHeight);
        settings_ = CanvasSettings{.sprites = std::move(game->sprites),
                                   .textures = std::move(game->textures),
                                   .emitters = std::move(game->emitters),
                                   .trails = std::move(game->trails),
                                   .beams = std::move(game->beams),
                                   .tilemapComponents = std::move(game->tilemaps)};
        // The game's tilemaps as its files read them, the maps its bodies
        // are made of (D858, D859).
        for (const world_kest::GameTilemapResource& each : files->tilemaps()) {
            if (each.map != nullptr) {
                settings_.tilemaps.emplace_back(each.id, *each.map);
            }
        }
        // The game's canvas materials from its cooked content (D356): one
        // of another domain is another renderer's; one that cannot be read,
        // or with no cooked content to read it from, is drawn as none, and
        // one that cannot be read is said so when the canvas starts.
        std::set<std::uint64_t> others;
        if (context.has(game_content::kGameContent.name) && !files->materials().empty()) {
            RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
            if (content->held()) {
                const std::array<content::AdmittedRepresentation, 1> kAdmitted = {content::AdmittedRepresentation{
                    .type = content::ResourceTypeId{material::kCanvasMaterialType},
                    .representation = *content::RepresentationId::parse(material::kCanvasMaterialRepresentation)}};
                RAWFRAME_TRY(content->admit(kAdmitted));
                for (const world_kest::GameMaterialResource& each : files->materials()) {
                    auto read = readCanvasMaterial(content->store(), each.material);
                    if (read.has_value()) {
                        settings_.materials.emplace_back(each.id, *read);
                    } else if (otherType(read.error())) {
                        others.insert(each.id);
                    } else if (!each.subasset) {
                        unreadMaterials_.emplace_back(each.path, std::string{read.error().description()});
                    }
                }
            }
        }
        for (const world_kest::GameMaterialResource& each : files->materials()) {
            if (!others.contains(each.id) && std::ranges::find(settings_.materials, each.id, [](const auto& entry) {
                                                 return entry.first;
                                             }) == settings_.materials.end()) {
                settings_.materials.emplace_back(each.id,
                                                 material::CanvasMaterial{.shading = material::Shading::Unlit});
            }
        }
        if (context.has(game_content::kGameContent.name) && context.cpuExecutor() != nullptr &&
            !files->textures().empty()) {
            RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
            if (!content->held()) {
                return {};
            }
            RAWFRAME_TRY(content->admit(game_textures::textureRepresentations()));
            auto textures = game_textures::GameTextures::create(content->store(),
                                                                *context.cpuExecutor(),
                                                                context.owner(),
                                                                context.scope(),
                                                                context.clock(),
                                                                files->textures(),
                                                                kTextureBudgetBytes);
            if (textures.has_value()) {
                textures_ = std::move(*textures);
            } else {
                unread_ = std::string{textures.error().description()};
            }
        }
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (unread_.has_value()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kUnread,
                         "the game's textures could not be asked for: every draw waits for its texture",
                         {diagnostics::field("reason", *unread_)});
        }
        for (const auto& [kPath, kReason] : unreadMaterials_) {
            emitter_.log(diagnostics::Severity::Warning,
                         kUnreadMaterial,
                         "a canvas material could not be read: its sprites are drawn without it",
                         {diagnostics::field("material", kPath), diagnostics::field("reason", kReason)});
        }
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& frame) noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        if (phase == composition::HostPhase::PresentationExtract) {
            ++tick_;
            queued_ = nullptr;
            for (CanvasRegion& each : regionFrames_) {
                each.frame = nullptr;
            }
            if (textures_ != nullptr) {
                const game_textures::TextureChanges kChanges = textures_->update(tick_);
                for (const auto& [kId, kError] : kChanges.failed) {
                    emitter_.log(diagnostics::Severity::Warning,
                                 kUnreadTexture,
                                 "a texture could not be read: its draws wait for it",
                                 {diagnostics::field("texture", identityText(kId)),
                                  diagnostics::field("reason", std::string{kError.description()})});
                }
                for (const std::uint64_t kId : kChanges.reloaded) {
                    ++reloaded_;
                    emitter_.log(diagnostics::Severity::Info,
                                 kTextureReloaded,
                                 "a texture was replaced by its new revision",
                                 {diagnostics::field("texture", identityText(kId))});
                }
                for (const auto& [kId, kError] : kChanges.notReloaded) {
                    emitter_.log(diagnostics::Severity::Warning,
                                 kTextureNotReloaded,
                                 "a texture's new revision could not be used: the old one is drawn on",
                                 {diagnostics::field("texture", identityText(kId)),
                                  diagnostics::field("reason", std::string{kError.description()})});
                }
                if (kChanges.read) {
                    emitter_.log(
                        diagnostics::Severity::Info,
                        kTexturesRead,
                        "the game's textures were read: every draw has its texture",
                        {diagnostics::field("textures", static_cast<std::uint64_t>(textures_->counts().ready))});
                }
            }
            extract();
        } else if (phase == composition::HostPhase::Present && extracted_) {
            extracted_ = false;
            // The seconds since the frame before, on the Host's timeline,
            // for the particle clock (D357): nought for the first.
            camera_.elapsed =
                presented_.has_value() ? static_cast<float>((frame.now - *presented_).nanoseconds) / 1e9F : 0.0F;
            presented_ = frame.now;
            if (!regions_.empty()) {
                presentPlayers(frame.now);
            }
            const CanvasFrame& kFrame = canvas_->queue(camera_);
            queued_ = &kFrame;
            if (!regionFrames_.empty() && regionFrames_[0].width != 0) {
                regionFrames_[0].frame = &kFrame;
            }
            tellViews();
            // What a device would draw this frame: the draws whose texture
            // and whose material's texture are decoded and held.
            const auto kWaits = [this](std::uint64_t id) {
                return id != 0 && (textures_ == nullptr || textures_->texture(id, tick_) == nullptr);
            };
            for (const CanvasDraw& draw : kFrame.draws) {
                if (kWaits(draw.texture) || kWaits(kFrame.materials[draw.material].sampled.id)) {
                    ++drawsWaiting_;
                }
            }
            ++frames_;
            drawn_ += kFrame.drawn;
            animated_ += kFrame.animated;
            culled_ += kFrame.culled;
            hidden_ += kFrame.hidden;
            malformed_ += kFrame.malformed;
            unknownTextures_ += kFrame.unknownTextures;
            unknownMaterials_ += kFrame.unknownMaterials;
            overLimit_ += kFrame.overLimit;
            mostDraws_ = std::max(mostDraws_, kFrame.draws.size());
            mostVertices_ = std::max(mostVertices_, kFrame.vertices.size());
            particles_.add(kFrame.particles, emitter_);
        }
    }

    void stop() noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        const game_textures::TextureCounts kTextures =
            textures_ != nullptr ? textures_->counts() : game_textures::TextureCounts{};
        std::vector<diagnostics::Field> fields = {
            diagnostics::field("frames", frames_),
            diagnostics::field("framesViewed", viewed_),
            diagnostics::field("framesPreviewed", previewed_),
            diagnostics::field("spritesHidden", canvas_ != nullptr ? canvas_->spritesHidden() : 0),
            diagnostics::field("viewWidth", width_),
            diagnostics::field("viewHeight", height_),
            diagnostics::field("spritesDrawn", drawn_),
            diagnostics::field("spritesAnimated", animated_),
            diagnostics::field("culled", culled_),
            diagnostics::field("hidden", hidden_),
            diagnostics::field("malformed", malformed_),
            diagnostics::field("unknownTextures", unknownTextures_),
            diagnostics::field("unknownMaterials", unknownMaterials_),
            diagnostics::field("overLimit", overLimit_),
            diagnostics::field("mostDraws", static_cast<std::uint64_t>(mostDraws_)),
            diagnostics::field("mostVertices", static_cast<std::uint64_t>(mostVertices_)),
            diagnostics::field("drawsWaiting", drawsWaiting_),
            diagnostics::field("texturesReady", static_cast<std::uint64_t>(kTextures.ready)),
            diagnostics::field("texturesFailed", static_cast<std::uint64_t>(kTextures.failed)),
            diagnostics::field("texturesReloaded", reloaded_),
            diagnostics::field("textureBytes", kTextures.bytes),
            diagnostics::field("players", static_cast<std::uint64_t>(localPlayers_.size() + 1)),
            diagnostics::field("playerFrames", playerFrames_),
            diagnostics::field("pathsShown", pathsDrawn_),
            diagnostics::field("outlinesShown", outlinesDrawn_)};
        const auto kParticles = particles_.fields();
        fields.insert(fields.end(), kParticles.begin(), kParticles.end());
        emitter_.log(diagnostics::Severity::Info, kCanvasSummary, "what one client's canvas drew", fields);
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kCanvasFrames.name) {
            return composition::provideAs<CanvasFrames>(*this);
        }
        return {};
    }

    const CanvasFrame* queued() const noexcept override {
        return queued_;
    }

    std::span<const CanvasRegion> regionFrames() const noexcept override {
        return regionFrames_;
    }

    std::array<std::uint8_t, 3> bars() const noexcept override {
        return aspect_.has_value() ? aspect_->bars : std::array<std::uint8_t, 3>{};
    }

    std::uint32_t width() const noexcept override {
        return width_;
    }

    std::uint32_t height() const noexcept override {
        return height_;
    }

    void resize(std::uint32_t width, std::uint32_t height) noexcept override {
        if (width == 0 || height == 0) {
            return;
        }
        width_ = width;
        height_ = height;
        camera_.aspect = static_cast<float>(width) / static_cast<float>(height);
    }

    std::shared_ptr<const texture::Texture> texture(std::uint64_t id) const override {
        return textures_ != nullptr ? textures_->texture(id, tick_) : nullptr;
    }

private:
    /// The client's World copied out, and the view moved to its player,
    /// through the player's camera if it has one.
    void extract() noexcept {
        const std::size_t kClient = client_.value_or(clients_->playerClient().value_or(0));
        const world_replication::ClientView kView = clients_->client(kClient);
        if (kView.world == nullptr) {
            return;
        }
        if (canvas_ == nullptr) {
            auto made = Canvas::create(kView.world->registry(), settings_);
            if (!made.has_value()) {
                clients_ = nullptr;
                return;
            }
            canvas_ = std::move(*made);
        }
        canvas_->hideLayers(preview_ != nullptr ? preview_->hidden() : std::vector<std::uint32_t>{});
        // The paths the previewed scene's splines make, cyan lines in the
        // canvas's plane (D891), and what each author has chosen, a
        // rectangle about each in their color (D907).
        bool linesChanged = false;
        if (preview_ != nullptr && preview_->pathsShown() != pathsSeen_) {
            pathsSeen_ = preview_->pathsShown();
            pathLines_.clear();
            pathsDrawn_ = 0;
            for (const std::vector<std::array<double, 3>>& kPath : preview_->paths()) {
                for (std::size_t at = 1; at < kPath.size(); ++at) {
                    pathLines_.push_back(CanvasLine{.from = {kPath[at - 1][0], kPath[at - 1][1]},
                                                    .to = {kPath[at][0], kPath[at][1]},
                                                    .color = 0x33D9FFFFU});
                }
                pathsDrawn_ += kPath.size() > 1 ? 1 : 0;
            }
            linesChanged = true;
        }
        if (preview_ != nullptr && preview_->outlinesShown() != outlinesSeen_) {
            outlinesSeen_ = preview_->outlinesShown();
            outlineLines_.clear();
            for (const view::Outline& kOutline : preview_->outlines()) {
                const auto kChannel = [&kOutline](std::size_t at) {
                    return static_cast<std::uint32_t>(std::lround(std::clamp(kOutline.color[at], 0.0F, 1.0F) * 255));
                };
                const std::uint32_t kColor = (kChannel(0) << 24U) | (kChannel(1) << 16U) | (kChannel(2) << 8U) | 0xFFU;
                for (const std::array<double, 6>& kBox : kOutline.boxes) {
                    const std::array<std::array<double, 2>, 4> kCorners = {
                        std::array<double, 2>{kBox[0] - kBox[3], kBox[1] - kBox[4]},
                        std::array<double, 2>{kBox[0] + kBox[3], kBox[1] - kBox[4]},
                        std::array<double, 2>{kBox[0] + kBox[3], kBox[1] + kBox[4]},
                        std::array<double, 2>{kBox[0] - kBox[3], kBox[1] + kBox[4]}};
                    for (std::size_t side = 0; side < 4; ++side) {
                        outlineLines_.push_back(
                            CanvasLine{.from = kCorners[side], .to = kCorners[(side + 1) % 4], .color = kColor});
                    }
                    ++outlinesDrawn_;
                }
            }
            linesChanged = true;
        }
        canvas_->extract(*kView.world);
        extracted_ = true;
        if (!kView.owned.isNull() && kView.world->alive(kView.owned) &&
            readCamera(*kView.world, kView.owned, camera_)) {
            ++viewed_;
        }
        // Where an editor's preview looks, in place of the player's camera.
        if (preview_ != nullptr && preview_->looking().has_value()) {
            camera_ = previewedCamera(*preview_->looking(), camera_);
            ++previewed_;
        }
        // The grid an editor's 2D view shows (D933), about where it looks,
        // made again only where the view moved.
        const bool kGrid = preview_ != nullptr && preview_->looking().has_value() &&
                           (preview_->overlays() & static_cast<std::uint8_t>(view::Overlay::Grid)) != 0;
        const std::array<double, 3> kGridAt{camera_.x, camera_.y, camera_.height};
        if (kGrid != gridShown_ || (kGrid && kGridAt != gridAt_)) {
            gridShown_ = kGrid;
            gridAt_ = kGridAt;
            gridLines_ = kGrid ? gridLinesAbout(camera_) : std::vector<CanvasLine>{};
            linesChanged = true;
        }
        if (linesChanged) {
            std::vector<CanvasLine> lines = gridLines_;
            lines.insert(lines.end(), pathLines_.begin(), pathLines_.end());
            lines.insert(lines.end(), outlineLines_.begin(), outlineLines_.end());
            canvas_->showLines(std::move(lines));
        }
        extractPlayers();
    }

    /// The camera on `entity` read into `camera`, the view placed by the
    /// entity's pose; whether it has one (else it sees as the default
    /// does).
    bool readCamera(world::World& world, world::EntityHandle entity, CanvasCamera& camera) const {
        Camera view{.height = kDefaultViewHeight};
        bool found = false;
        if (cameraComponent_.has_value()) {
            if (const auto kCamera = world.registry().find(*cameraComponent_)) {
                if (const auto* placed = static_cast<const Camera*>(world.getErased(entity, *kCamera))) {
                    view = *placed;
                    found = true;
                }
            }
        }
        camera.height = view.height;
        if (const auto kPose = world.registry().key<physics2d::Pose2D>()) {
            if (const auto* pose = world.get(entity, *kPose)) {
                camera.x = pose->x + view.offsetX;
                camera.y = pose->y + view.offsetY;
            }
        }
        return found;
    }

    /// The other local players' Worlds (D364), each extracted into its own
    /// canvas and its camera read from its player.
    void extractPlayers() {
        for (LocalPlayer& each : localPlayers_) {
            each.extracted = false;
            const world_replication::ClientView kView = clients_->client(each.client);
            if (kView.world == nullptr) {
                continue;
            }
            if (each.canvas == nullptr) {
                auto made = Canvas::create(kView.world->registry(), settings_);
                if (!made.has_value()) {
                    continue;
                }
                each.canvas = std::move(*made);
            }
            each.canvas->extract(*kView.world);
            if (!kView.owned.isNull() && kView.world->alive(kView.owned)) {
                static_cast<void>(readCamera(*kView.world, kView.owned, each.camera));
            }
            each.extracted = true;
        }
    }

    /// Each local player's region placed in the window as it is now, the
    /// first's camera given its aspect, the others' frames queued (D364).
    void presentPlayers(execution::MonotonicInstant now) {
        for (std::size_t at = 0; at < regions_.size(); ++at) {
            const world_kest::RegionPixels kWhole = world_kest::pixelsOf(regions_[at], width_, height_);
            const world_kest::RegionPixels kPixels =
                aspect_.has_value() ? world_kest::constrainedTo(kWhole, *aspect_) : kWhole;
            regionFrames_[at] =
                CanvasRegion{.x = kPixels.x, .y = kPixels.y, .width = kPixels.width, .height = kPixels.height};
        }
        if (regionFrames_[0].width != 0 && regionFrames_[0].height != 0) {
            camera_.aspect = static_cast<float>(regionFrames_[0].width) / static_cast<float>(regionFrames_[0].height);
        }
        for (std::size_t at = 0; at < localPlayers_.size(); ++at) {
            LocalPlayer& each = localPlayers_[at];
            CanvasRegion& region = regionFrames_[at + 1];
            if (!each.extracted || region.width == 0 || region.height == 0) {
                each.presented.reset();
                continue;
            }
            each.camera.aspect = static_cast<float>(region.width) / static_cast<float>(region.height);
            each.camera.elapsed =
                each.presented.has_value() ? static_cast<float>((now - *each.presented).nanoseconds) / 1e9F : 0.0F;
            each.presented = now;
            region.frame = &each.canvas->queue(each.camera);
            ++playerFrames_;
        }
    }

    /// Each local player's view as this frame derived it (D367), as the
    /// scene tells its own.
    void tellViews() {
        if (views_ == nullptr) {
            return;
        }
        const auto kRegionOf = [&](std::size_t at) {
            if (regionFrames_.empty()) {
                return view::Region{};
            }
            const CanvasRegion& kRegion = regionFrames_[at];
            const auto kWidth = static_cast<float>(width_);
            const auto kHeight = static_cast<float>(height_);
            return view::Region{.left = static_cast<float>(kRegion.x) / kWidth,
                                .top = static_cast<float>(kRegion.y) / kHeight,
                                .width = static_cast<float>(kRegion.width) / kWidth,
                                .height = static_cast<float>(kRegion.height) / kHeight};
        };
        views_->tell(0, kRegionOf(0), orthographicOf(camera_));
        for (std::size_t at = 0; at < localPlayers_.size(); ++at) {
            if (localPlayers_[at].presented.has_value()) {
                views_->tell(at + 1, kRegionOf(at + 1), orthographicOf(localPlayers_[at].camera));
            } else {
                views_->forgetOrthographic(at + 1);
            }
        }
    }

    world_replication::ClientWorlds* clients_ = nullptr;
    std::optional<std::size_t> client_;
    /// The local players' views, told each frame (D367).
    view::PlayerViews* views_ = nullptr;
    view::PreviewCamera* preview_ = nullptr;
    CanvasSettings settings_;
    CanvasCamera camera_;
    std::optional<execution::MonotonicInstant> presented_;
    std::optional<schema::ComponentTypeId> cameraComponent_;
    /// Split-screen (D364): the layout's regions, the local players after
    /// the first (whose are `canvas_` and `camera_`), each region's frame,
    /// and the frames queued for the others.
    struct LocalPlayer {
        std::size_t client = 0;
        std::unique_ptr<Canvas> canvas;
        CanvasCamera camera;
        std::optional<execution::MonotonicInstant> presented;
        bool extracted = false;
    };
    std::vector<world_kest::GameRegion> regions_;
    /// The game's constrained aspect, each region's view centered between
    /// bars (D369); none fills.
    std::optional<world_kest::GameAspect> aspect_;
    std::vector<LocalPlayer> localPlayers_;
    std::vector<CanvasRegion> regionFrames_;
    std::uint64_t playerFrames_ = 0;
    /// Frames seen through the player's own camera.
    std::uint64_t viewed_ = 0;
    std::uint64_t reloaded_ = 0;
    std::unique_ptr<Canvas> canvas_;
    std::unique_ptr<game_textures::GameTextures> textures_;
    std::optional<std::string> unread_;
    std::uint64_t tick_ = 0;
    std::uint64_t drawsWaiting_ = 0;
    bool extracted_ = false;
    const CanvasFrame* queued_ = nullptr;
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 720;
    std::uint64_t frames_ = 0;
    std::uint64_t drawn_ = 0;
    std::uint64_t animated_ = 0;
    std::uint64_t culled_ = 0;
    std::uint64_t previewed_ = 0;
    /// The preview's paths last seen, and how many it drew (D891).
    std::uint64_t pathsSeen_ = 0;
    std::uint64_t pathsDrawn_ = 0;
    std::vector<CanvasLine> pathLines_;
    /// The 2D view's grid, whether it is shown, and where it was made.
    std::vector<CanvasLine> gridLines_;
    bool gridShown_ = false;
    std::array<double, 3> gridAt_{};
    std::vector<CanvasLine> outlineLines_;
    std::uint64_t outlinesSeen_ = 0;
    std::uint64_t outlinesDrawn_ = 0;
    std::uint64_t hidden_ = 0;
    std::uint64_t malformed_ = 0;
    std::uint64_t unknownTextures_ = 0;
    std::uint64_t unknownMaterials_ = 0;
    /// The particles' frames, summed (D360).
    rawframe::particles::Tally particles_;
    std::vector<std::pair<std::string, std::string>> unreadMaterials_;
    std::uint64_t overLimit_ = 0;
    std::size_t mostDraws_ = 0;
    std::size_t mostVertices_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<CanvasParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.render_canvas.canvas",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvided,
        .optionalCapabilities = kMaybe,
        // Textures are decoded on the CPU executor.
        .executor = {.cpu = true, .quota = {.maximumPendingTasks = 64}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "render_canvas.canvas",
        .budgetOwner = "render",
        .hostPhases =
            static_cast<std::uint16_t>(composition::hostPhaseBit(composition::HostPhase::PresentationExtract) |
                                       composition::hostPhaseBit(composition::HostPhase::Present)),
    });
}

} // namespace rawframe::render_canvas
