#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/graph/graph.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene_gpu/captures.h"
#include "rawframe/scene_bake/bake.h"
#include "rawframe/scene_bake/registrar.h"
#include "rawframe/world_kest/game_files.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rawframe::scene_bake {

namespace {

constexpr diagnostics::EventIdentity kBaked{"bake", "probe_baked"};
constexpr diagnostics::EventIdentity kNotBaked{"bake", "probe_not_baked"};
constexpr diagnostics::EventIdentity kSummary{"bake", "bake_summary"};
constexpr std::string_view kMaybe[] = {
    render_scene::kSceneFrames.name, render_scene_gpu::kSceneCaptures.name, world_kest::kGameFiles.name};

std::unexpected<result::Error> badConfiguration(std::string_view why) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                       composition::kCompositionDomain,
                                                       code(composition::CompositionError::BadConfiguration),
                                                       why)
                                              .error()};
}

/// A probe to bake: where its box's middle is, and the texture its
/// picture is.
struct Wanted {
    std::array<double, 3> position{};
    std::uint64_t environment = 0;
    std::string path;
};

/// Bakes the scene's reflection probes (D326), one face a capture: the
/// scene queued from the face's camera, drawn with no probe reflected, so
/// no probe's old picture is baked into another's.
class BakeParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const std::optional<std::string> kDirectory = configuration.path("bake.directory");
        RAWFRAME_TRY_ASSIGN(after_, configuration.unsignedInteger("bake.after", 120));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kExposure, configuration.unsignedInteger("bake.exposure", 10));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kWidth, configuration.unsignedInteger("bake.width", 512));
        if (kExposure > 24) {
            return badConfiguration("bake.exposure is an EV100 from 0 to 24");
        }
        if (kWidth < 16 || kWidth > 8192 || kWidth % 2 != 0) {
            return badConfiguration("bake.width is an even number of texels from 16 to 8192");
        }
        if (!kDirectory.has_value()) {
            return {};
        }
        if (!context.has(render_scene::kSceneFrames.name) || !context.has(render_scene_gpu::kSceneCaptures.name) ||
            !context.has(world_kest::kGameFiles.name)) {
            return badConfiguration("bake.directory needs a game's scene drawn on a device");
        }
        directory_ = *kDirectory;
        exposure_ = static_cast<float>(kExposure);
        width_ = static_cast<std::uint32_t>(kWidth);
        RAWFRAME_TRY_ASSIGN(scene_, context.capability(render_scene::kSceneFrames));
        RAWFRAME_TRY_ASSIGN(captures_, context.capability(render_scene_gpu::kSceneCaptures));
        RAWFRAME_TRY_ASSIGN(const world_kest::GameFiles* files, context.capability(world_kest::kGameFiles));
        textures_ = files->textures();
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase /*phase*/, const composition::HostFrame& /*frame*/) noexcept override {
        if (scene_ == nullptr || done_) {
            return;
        }
        if (presented_ < after_) {
            ++presented_;
            return;
        }
        if (!listed_) {
            list();
            listed_ = true;
        }
        if (at_ >= wanted_.size()) {
            done_ = true;
            return;
        }
        if (pending_) {
            std::optional<render_scene_gpu::LightCapture> light = captures_->captured();
            if (!light.has_value()) {
                return;
            }
            faces_[face_].light = std::move(*light);
            pending_ = false;
            if (++face_ == faces_.size()) {
                write(wanted_[at_]);
                face_ = 0;
                ++at_;
            }
            return;
        }
        const float kAspect = static_cast<float>(scene_->width()) / static_cast<float>(std::max(scene_->height(), 1U));
        const render_scene::SceneCamera kCamera = faceCameras(wanted_[at_].position, kAspect, exposure_)[face_];
        const render_scene::SceneFrame* queued = scene_->queueFrom(kCamera);
        if (queued == nullptr) {
            return;
        }
        render_scene::SceneFrame frame = *queued;
        frame.probes.clear();
        frame.probesOverLimit = 0;
        for (render_scene::SceneDraw& draw : frame.draws) {
            draw.probe = 0;
        }
        frame.temporal.enabled = false;
        frame.temporal.history = false;
        frame.fxaa = false;
        frame.metering.enabled = false;
        faces_[face_] = BakedFace{.view = frame.view, .projection = frame.projection};
        pending_ = captures_->capture(std::move(frame));
    }

    void stop() noexcept override {
        if (scene_ == nullptr) {
            return;
        }
        emitter_.log(diagnostics::Severity::Info,
                     kSummary,
                     "the reflection probes baked",
                     {diagnostics::field("probes", static_cast<std::uint64_t>(wanted_.size())),
                      diagnostics::field("baked", baked_),
                      diagnostics::field("notBaked", notBaked_)});
    }

private:
    /// The probes the scene extracted, one for each picture, those whose
    /// picture the game declares.
    void list() {
        for (const render_scene::ProbeInstance& kProbe : scene_->probes()) {
            const std::uint64_t kId = kProbe.probe.environment;
            if (kId == 0 || std::ranges::contains(wanted_, kId, &Wanted::environment)) {
                continue;
            }
            const auto kDeclared = std::ranges::find(textures_, kId, &world_kest::GameTextureResource::id);
            if (kDeclared == textures_.end()) {
                ++notBaked_;
                emitter_.log(diagnostics::Severity::Warning,
                             kNotBaked,
                             "a reflection probe names a picture the game does not declare: it is not baked",
                             {diagnostics::field("texture", graph::nodeIdText(kId))});
                continue;
            }
            wanted_.push_back(Wanted{.position = kProbe.position, .environment = kId, .path = kDeclared->path});
        }
    }

    /// The probe's six faces resampled into its picture, written as
    /// Radiance where its texture is.
    void write(const Wanted& probe) {
        const std::optional<texture_import::LightImage> kPicture = pictureOf(faces_, width_);
        const std::filesystem::path kFile = std::filesystem::path{directory_} / probe.path;
        std::error_code error;
        std::filesystem::create_directories(kFile.parent_path(), error);
        bool written = false;
        if (kPicture.has_value()) {
            const std::vector<std::byte> kBytes = texture_import::encodeRadiance(*kPicture);
            std::ofstream file{kFile, std::ios::binary | std::ios::trunc};
            file.write(reinterpret_cast<const char*>(kBytes.data()), static_cast<std::streamsize>(kBytes.size()));
            written = static_cast<bool>(file);
        }
        if (!written) {
            ++notBaked_;
            emitter_.log(diagnostics::Severity::Warning,
                         kNotBaked,
                         "a reflection probe's picture could not be written",
                         {diagnostics::field("texture", graph::nodeIdText(probe.environment)),
                          diagnostics::field("path", kFile.string())});
            return;
        }
        ++baked_;
        emitter_.log(diagnostics::Severity::Info,
                     kBaked,
                     "a reflection probe's picture baked",
                     {diagnostics::field("texture", graph::nodeIdText(probe.environment)),
                      diagnostics::field("path", kFile.string()),
                      diagnostics::field("width", std::uint64_t{width_})});
    }

    render_scene::SceneFrames* scene_ = nullptr;
    render_scene_gpu::SceneCaptures* captures_ = nullptr;
    std::vector<world_kest::GameTextureResource> textures_;
    std::string directory_;
    float exposure_ = 10;
    std::uint32_t width_ = 512;
    std::uint64_t after_ = 120;
    std::uint64_t presented_ = 0;
    bool listed_ = false;
    bool done_ = false;
    std::vector<Wanted> wanted_;
    /// The probe being baked, its face, and whether that face's capture
    /// is awaited.
    std::size_t at_ = 0;
    std::size_t face_ = 0;
    bool pending_ = false;
    std::array<BakedFace, 6> faces_{};
    std::uint64_t baked_ = 0;
    std::uint64_t notBaked_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<BakeParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.scene_bake.probes",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = composition::only(composition::TargetRole::Tool)},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "scene_bake.probes",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
}

} // namespace rawframe::scene_bake
