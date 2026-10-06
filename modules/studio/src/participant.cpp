#include "generated/studio_font.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/studio/registrar.h"
#include "rawframe/ui/frames.h"
#include "rawframe/ui/tree.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::studio {

namespace {

constexpr diagnostics::EventIdentity kSummary{"studio", "studio_summary"};
constexpr std::string_view kProvided[] = {ui::kUiFrames.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

/// Studio's colors, 0xRRGGBBAA: the window behind everything, a panel, a
/// row, and the header.
constexpr std::uint32_t kBackground = 0x1E1F24FFU;
constexpr std::uint32_t kPanel = 0x2A2C33FFU;
constexpr std::uint32_t kRow = 0x3A3D47FFU;
constexpr std::uint32_t kHeaderFill = 0x30343FFFU;
constexpr std::uint32_t kText = 0xE6E8EEFFU;
constexpr std::uint32_t kQuiet = 0x9AA0ADFFU;

result::Status misconfigured(std::string_view why) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                       composition::kCompositionDomain,
                                                       code(composition::CompositionError::BadConfiguration),
                                                       why)
                                              .error()};
}

/// The shell: a session on the game, and the UI that shows it.
class ShellParticipant final : public composition::Participant, public ui::UiFrames {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const auto kGame = configuration.path("studio.game");
        if (!kGame.has_value()) {
            return misconfigured("Studio needs studio.game, the game description its session reads");
        }
        const std::filesystem::path kDescription{std::string{*kGame}};
        const std::filesystem::path kRoot{
            std::string{configuration.path("studio.root").value_or(kDescription.parent_path().string())}};
        session_ = std::make_unique<authoring_session::Session>(kDescription, kRoot);
        bool ended = false;
        const std::string kWelcome =
            session_->answer(R"({"kind":"authoring.hello","id":1,"surfaceGeneration":1})", ended);
        ++records_;
        if (kWelcome.find("\"authoring.welcome\"") == std::string::npos) {
            return misconfigured("Studio's session did not welcome it: the game does not read");
        }
        for (const auto& [kIdentity, kPath] : authoring_session::scenesBeside(kDescription)) {
            std::error_code error;
            scenes_.push_back(std::filesystem::relative(kPath, kRoot, error).generic_string());
        }
        std::ranges::sort(scenes_);
        title_ = "Rawframe Studio  " + kDescription.filename().string();
        RAWFRAME_TRY_ASSIGN(tree_, ui::Tree::create());
        // Studio's own font, embedded, sanitized as the cook sanitizes a
        // game's (ADR-0049), never an unchecked font in the tree.
        const std::span<const std::byte> kFontBytes = std::as_bytes(std::span{kStudioFont});
        RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kSanitized, font_import::sanitize(kFontBytes));
        RAWFRAME_TRY_ASSIGN(font_, tree_->addFont(kSanitized));
        RAWFRAME_TRY(tree_->setDefaultFont(font_));
        return build();
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase, const composition::HostFrame&) noexcept override {
        drawn_ = nullptr;
        if (!tree_->layOut(root_, static_cast<float>(width_), static_cast<float>(height_)).has_value()) {
            return;
        }
        list_ = {};
        if (tree_->draw(root_, 1.0F, list_).has_value()) {
            drawn_ = &list_;
            ++framesDrawn_;
        }
    }

    void stop() noexcept override {
        emitter_.log(diagnostics::Severity::Info,
                     kSummary,
                     "what Studio showed",
                     {diagnostics::field("records", records_),
                      diagnostics::field("scenes", static_cast<std::uint64_t>(scenes_.size())),
                      diagnostics::field("framesDrawn", framesDrawn_),
                      diagnostics::field("boxes", static_cast<std::uint64_t>(list_.boxes.size())),
                      diagnostics::field("glyphs", static_cast<std::uint64_t>(list_.glyphs.size()))});
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == ui::kUiFrames.name) {
            return composition::provideAs<ui::UiFrames>(*this);
        }
        return {};
    }

    const ui::DrawList* drawn() const noexcept override {
        return drawn_;
    }

    void resize(std::uint32_t width, std::uint32_t height) noexcept override {
        if (width != 0 && height != 0) {
            width_ = width;
            height_ = height;
        }
    }

    std::shared_ptr<const texture::Texture> image(std::uint64_t) const override {
        return nullptr;
    }

private:
    /// A box of `color` as `layout` places it under `parent`, if any.
    result::Result<ui::Node> box(std::optional<ui::Node> parent, const ui::Layout& layout, std::uint32_t color) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kNode, tree_->add(++keys_));
        RAWFRAME_TRY(tree_->setLayout(kNode, layout));
        RAWFRAME_TRY(tree_->setLook(kNode, ui::Look{.fill = color, .radius = 4}));
        if (parent.has_value()) {
            RAWFRAME_TRY(tree_->attach(*parent, kNode));
        }
        return kNode;
    }

    /// Words on `node`, in Studio's font.
    result::Status words(ui::Node node, std::string_view text, std::uint32_t color, float size = 15) {
        return tree_->setText(node, text, ui::TextLook{.font = font_, .size = size, .color = color, .wrap = false});
    }

    /// A column under `parent` headed `heading`.
    result::Result<ui::Node> column(ui::Node parent, std::string_view heading) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kColumn,
                            box(parent,
                                ui::Layout{.width = ui::pixels(0),
                                           .direction = ui::Direction::Column,
                                           .gap = 4,
                                           .grow = 1,
                                           .padding = {8, 8, 8, 8}},
                                kPanel));
        RAWFRAME_TRY_ASSIGN(const ui::Node kHeading, box(kColumn, ui::Layout{.height = ui::pixels(22)}, 0));
        RAWFRAME_TRY(words(kHeading, heading, kQuiet, 13));
        return kColumn;
    }

    /// The window: a header over three columns, the scenes' with a row for
    /// each scene beside the game.
    result::Status build() {
        RAWFRAME_TRY_ASSIGN(root_,
                            box(std::nullopt,
                                ui::Layout{.width = ui::share(1),
                                           .height = ui::share(1),
                                           .direction = ui::Direction::Column,
                                           .gap = 6,
                                           .padding = {6, 6, 6, 6}},
                                kBackground));
        RAWFRAME_TRY_ASSIGN(const ui::Node kHeader,
                            box(root_, ui::Layout{.height = ui::pixels(34), .padding = {12, 6, 12, 6}}, kHeaderFill));
        RAWFRAME_TRY(words(kHeader, title_, kText));
        RAWFRAME_TRY_ASSIGN(const ui::Node kColumns,
                            box(root_, ui::Layout{.direction = ui::Direction::Row, .gap = 6, .grow = 1}, kBackground));
        RAWFRAME_TRY_ASSIGN(const ui::Node kScenes, column(kColumns, "Scenes"));
        RAWFRAME_TRY(column(kColumns, "Entities"));
        RAWFRAME_TRY(column(kColumns, "Components"));
        for (const std::string& kScene : scenes_) {
            RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                                box(kScenes, ui::Layout{.height = ui::pixels(28), .padding = {8, 4, 8, 4}}, kRow));
            RAWFRAME_TRY(words(kRowNode, kScene, kText, 14));
        }
        return {};
    }

    std::unique_ptr<authoring_session::Session> session_;
    std::unique_ptr<ui::Tree> tree_;
    ui::Node root_{};
    std::uint64_t keys_ = 0;
    ui::DrawList list_;
    const ui::DrawList* drawn_ = nullptr;
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 720;
    std::vector<std::string> scenes_;
    std::string title_;
    ui::Font font_{};
    std::uint64_t records_ = 0;
    std::uint64_t framesDrawn_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<ShellParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.studio.shell",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvided,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "studio.shell",
        .budgetOwner = "ui",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
}

} // namespace rawframe::studio
