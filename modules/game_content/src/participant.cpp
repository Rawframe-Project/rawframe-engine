#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/content/errors.h"
#include "rawframe/game_content/cooked_content.h"
#include "rawframe/game_content/registrar.h"

#include <iterator>
#include <string>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#endif

namespace rawframe::game_content {

namespace {

constexpr diagnostics::EventIdentity kReloaded{"content", "content_reloaded"};
constexpr diagnostics::EventIdentity kRefused{"content", "content_reload_refused"};
constexpr diagnostics::EventIdentity kOpened{"content", "composition_opened"};
constexpr std::string_view kProvided[] = {kGameContent.name};

/// Holds the game's cooked content for the Runtime and, when asked to,
/// publishes a changed manifest between frames.
class ContentParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        RAWFRAME_TRY_ASSIGN(reloadEvery_, configuration.unsignedInteger("content.reload_every", 0));
        if (context.blockingIoExecutor() == nullptr) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                               content::kContentDomain,
                                                               code(content::ContentError::SourceUnavailable),
                                                               "cooked content is read on the blocking-I/O executor")
                                                      .error()};
        }
        // A host that holds files names its Composition among them (D167),
        // as a web client holds what it fetched.
        if (const composition::HeldFiles* held = context.heldFiles()) {
            return loadHeld(context, *held);
        }
#if !RAWFRAME_FILE_SYSTEM
        // Without files, content is held by the host that fetched it, not
        // named by a path.
        if (configuration.path("content.root") || configuration.path("content.composition")) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::FailedPrecondition,
                             content::kContentDomain,
                             code(content::ContentError::SourceUnavailable),
                             "content.root and content.composition name files, and there are none here")
                    .error()};
        }
        RAWFRAME_TRY_ASSIGN(
            content_,
            CookedContent::none(*context.blockingIoExecutor(), context.owner(), context.scope(), context.clock()));
        return {};
#else
        std::optional<std::filesystem::path> root;
        if (const auto kRoot = configuration.path("content.root")) {
            root = std::filesystem::path{std::string{*kRoot}};
        }
        // A Composition, or a cook's output, or nothing.
        if (const auto kComposition = configuration.path("content.composition")) {
            const auto kLibrary = configuration.path("content.library");
            if (root.has_value() || !kLibrary.has_value()) {
                return std::unexpected<result::Error>{
                    result::fail(result::ErrorClass::InvalidArgument,
                                 content::kContentDomain,
                                 code(content::ContentError::SourceUnavailable),
                                 "a Composition is named by content.composition and read from content.library, "
                                 "without content.root")
                        .error()};
            }
            std::ifstream file{std::string{*kComposition}, std::ios::binary};
            const std::string kRecord{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
            RAWFRAME_TRY_ASSIGN(content_,
                                CookedContent::openComposition(*context.blockingIoExecutor(),
                                                               context.owner(),
                                                               context.scope(),
                                                               context.clock(),
                                                               kRecord,
                                                               std::filesystem::path{std::string{*kLibrary}}));
            return {};
        }
        RAWFRAME_TRY_ASSIGN(
            content_,
            CookedContent::open(
                *context.blockingIoExecutor(), context.owner(), context.scope(), context.clock(), std::move(root)));
        return {};
#endif
    }

    /// A Composition's record and library from the files the host holds;
    /// a cook's output is a directory's, never held.
    result::Status loadHeld(composition::ParticipantContext& context, const composition::HeldFiles& held) {
        const composition::Configuration& configuration = context.configuration();
        const auto kComposition = configuration.path("content.composition");
        const auto kLibrary = configuration.path("content.library");
        if (configuration.path("content.root")) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               content::kContentDomain,
                                                               code(content::ContentError::SourceUnavailable),
                                                               "a cook's output is not held; hold a Composition")
                                                      .error()};
        }
        if (!kComposition) {
            RAWFRAME_TRY_ASSIGN(
                content_,
                CookedContent::none(*context.blockingIoExecutor(), context.owner(), context.scope(), context.clock()));
            return {};
        }
        const std::vector<std::byte>* record = held.find(*kComposition);
        if (record == nullptr || !kLibrary) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             content::kContentDomain,
                             code(content::ContentError::SourceUnavailable),
                             "a held Composition is named by content.composition and read from content.library")
                    .error()};
        }
        RAWFRAME_TRY_ASSIGN(content_,
                            CookedContent::openComposition(
                                *context.blockingIoExecutor(),
                                context.owner(),
                                context.scope(),
                                context.clock(),
                                std::string_view{reinterpret_cast<const char*>(record->data()), record->size()},
                                held.under(*kLibrary)));
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (const auto& kId = content_->compositionId()) {
            emitter_.log(diagnostics::Severity::Info,
                         kOpened,
                         "the Runtime's content is a Composition",
                         {diagnostics::field("composition", content::ContentDigest{.bytes = *kId}.text())});
        }
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
