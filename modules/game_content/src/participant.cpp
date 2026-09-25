#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/content/errors.h"
#include "rawframe/game_content/cooked_content.h"
#include "rawframe/game_content/registrar.h"

#include <string>

namespace rawframe::game_content {

namespace {

constexpr diagnostics::EventIdentity kReloaded{"content", "content_reloaded"};
constexpr diagnostics::EventIdentity kRefused{"content", "content_reload_refused"};
constexpr std::string_view kProvided[] = {kGameContent.name};

/// Holds the game's cooked content for the Runtime and, when asked to,
/// publishes a changed manifest between frames.
class ContentParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        std::optional<std::filesystem::path> root;
        if (const auto kRoot = configuration.text("content.root")) {
            root = std::filesystem::path{std::string{*kRoot}};
        }
        RAWFRAME_TRY_ASSIGN(reloadEvery_, configuration.unsignedInteger("content.reload_every", 0));
        if (context.blockingIoExecutor() == nullptr) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                               content::kContentDomain,
                                                               code(content::ContentError::SourceUnavailable),
                                                               "cooked content is read on the blocking-I/O executor")
                                                      .error()};
        }
        RAWFRAME_TRY_ASSIGN(
            content_,
            CookedContent::open(
                *context.blockingIoExecutor(), context.owner(), context.scope(), context.clock(), std::move(root)));
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kGameContent.name) {
            return composition::provideAs<GameContent>(*content_);
        }
        return {};
    }

    /// Every `content.reload_every` Host iterations: a changed manifest
    /// becomes the next catalog, and every family's assets reconcile
    /// against it at their next update.
    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (reloadEvery_ == 0 || frame.iteration % reloadEvery_ != 0) {
            return;
        }
        const auto kRefreshed = content_->refresh();
        if (!kRefreshed.has_value()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kRefused,
                         "a changed manifest was not published; the running catalog stays",
                         {diagnostics::field("reason", std::string{kRefreshed.error().description()})});
            return;
        }
        if (*kRefreshed) {
            emitter_.log(diagnostics::Severity::Info,
                         kReloaded,
                         "a changed manifest was published as the next catalog",
                         {diagnostics::field("generation", content_->generation())});
        }
    }

private:
    std::unique_ptr<CookedContent> content_;
    std::uint64_t reloadEvery_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makeContent(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<ContentParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.game_content",
        .factory = &makeContent,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvided,
        // Reads are the store's, on the blocking-I/O executor.
        .executor = {.blockingIo = true, .quota = {.maximumPendingTasks = 64}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(500)},
        .observabilityIdentity = "game_content",
        .budgetOwner = "content",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Maintenance),
    });
}

} // namespace rawframe::game_content
