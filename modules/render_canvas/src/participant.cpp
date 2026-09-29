#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/render_canvas/canvas.h"
#include "rawframe/render_canvas/errors.h"
#include "rawframe/render_canvas/registrar.h"
#include "rawframe/render_canvas/textures.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

namespace rawframe::render_canvas {

namespace {

constexpr diagnostics::EventIdentity kCanvasSummary{"canvas", "canvas_summary"};
constexpr diagnostics::EventIdentity kUnread{"canvas", "textures_unavailable"};
constexpr diagnostics::EventIdentity kUnreadTexture{"canvas", "texture_unavailable"};
constexpr std::string_view kMaybe[] = {
    world_replication::kClientWorlds.name, world_kest::kGameFiles.name, game_content::kGameContent.name};
/// The decoded levels the canvas holds at most.
constexpr std::uint64_t kTextureBudgetBytes = std::uint64_t{256} * 1024 * 1024;

/// A texture's identity as its game writes it, 16 hexadecimal digits.
std::string identityText(std::uint64_t id) {
    std::string text(16, '0');
    for (std::size_t at = 0; at < 16; ++at) {
        text[15 - at] = "0123456789abcdef"[(id >> (4 * at)) & 0xFU];
    }
    return text;
}

/// Draws one client's mirrored World each frame through a camera following
/// its player: the extract stage in `presentation_extract`, the queue stage
/// in `present`. Idle without a game that has sprites or without clients.
/// It reads the game's textures from the Runtime's cooked content
/// (`rawframe.content.game`) when the process has some; without it, it
/// draws on, every draw waiting for its texture.
class CanvasParticipant final : public composition::Participant {
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
        camera_.height = game->cameraHeight;
        camera_.aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
        settings_ = CanvasSettings{.sprite = game->sprite, .textures = std::move(game->textures)};
        if (context.has(game_content::kGameContent.name) && context.cpuExecutor() != nullptr &&
            !files->textures().empty()) {
            RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
            if (!content->held()) {
                return {};
            }
            RAWFRAME_TRY(content->admit(textureRepresentations()));
            auto textures = CanvasTextures::create(content->store(),
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
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& /*frame*/) noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        if (phase == composition::HostPhase::PresentationExtract) {
            ++tick_;
            if (textures_ != nullptr) {
                for (const auto& [kId, kError] : textures_->update(tick_)) {
                    emitter_.log(diagnostics::Severity::Warning,
                                 kUnreadTexture,
                                 "a texture could not be read: its draws wait for it",
                                 {diagnostics::field("texture", identityText(kId)),
                                  diagnostics::field("reason", std::string{kError.description()})});
                }
            }
            extract();
        } else if (phase == composition::HostPhase::Present && extracted_) {
            extracted_ = false;
            const CanvasFrame& kFrame = canvas_->queue(camera_);
            // What a device would draw this frame: the draws whose texture
            // is decoded and held.
            for (const CanvasDraw& draw : kFrame.draws) {
                if (textures_ == nullptr || textures_->texture(draw.texture, tick_) == nullptr) {
                    ++drawsWaiting_;
                }
            }
            ++frames_;
            drawn_ += kFrame.drawn;
            culled_ += kFrame.culled;
            hidden_ += kFrame.hidden;
            malformed_ += kFrame.malformed;
            unknownTextures_ += kFrame.unknownTextures;
            overLimit_ += kFrame.overLimit;
            mostDraws_ = std::max(mostDraws_, kFrame.draws.size());
            mostVertices_ = std::max(mostVertices_, kFrame.vertices.size());
        }
    }

    void stop() noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        const TextureCounts kTextures = textures_ != nullptr ? textures_->counts() : TextureCounts{};
        emitter_.log(diagnostics::Severity::Info,
                     kCanvasSummary,
                     "what one client's canvas drew",
                     {diagnostics::field("frames", frames_),
                      diagnostics::field("spritesDrawn", drawn_),
                      diagnostics::field("culled", culled_),
                      diagnostics::field("hidden", hidden_),
                      diagnostics::field("malformed", malformed_),
                      diagnostics::field("unknownTextures", unknownTextures_),
                      diagnostics::field("overLimit", overLimit_),
                      diagnostics::field("mostDraws", static_cast<std::uint64_t>(mostDraws_)),
                      diagnostics::field("mostVertices", static_cast<std::uint64_t>(mostVertices_)),
                      diagnostics::field("drawsWaiting", drawsWaiting_),
                      diagnostics::field("texturesReady", static_cast<std::uint64_t>(kTextures.ready)),
                      diagnostics::field("texturesFailed", static_cast<std::uint64_t>(kTextures.failed)),
                      diagnostics::field("textureBytes", kTextures.bytes)});
    }

private:
    /// The client's World copied out, and the camera moved to its player.
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
        canvas_->extract(*kView.world);
        extracted_ = true;
        if (kView.owned.isNull() || !kView.world->alive(kView.owned)) {
            return;
        }
        if (const auto kPose = kView.world->registry().key<physics2d::Pose2D>()) {
            if (const auto* pose = kView.world->get(kView.owned, *kPose)) {
                camera_.x = pose->x;
                camera_.y = pose->y;
            }
        }
    }

    world_replication::ClientWorlds* clients_ = nullptr;
    std::optional<std::size_t> client_;
    CanvasSettings settings_;
    CanvasCamera camera_;
    std::unique_ptr<Canvas> canvas_;
    std::unique_ptr<CanvasTextures> textures_;
    std::optional<std::string> unread_;
    std::uint64_t tick_ = 0;
    std::uint64_t drawsWaiting_ = 0;
    bool extracted_ = false;
    std::uint64_t frames_ = 0;
    std::uint64_t drawn_ = 0;
    std::uint64_t culled_ = 0;
    std::uint64_t hidden_ = 0;
    std::uint64_t malformed_ = 0;
    std::uint64_t unknownTextures_ = 0;
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
