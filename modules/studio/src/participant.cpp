#include "generated/studio_font.h"
#include "rawframe/authoring_session/attach.h"
#include "rawframe/process/self.h"
#include "shell.h"

namespace rawframe::studio {

namespace {

constexpr std::string_view kProvided[] = {ui::kUiFrames.name};
constexpr std::string_view kMaybe[] = {view::kUiPointing.name, view::kUiTyping.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

result::Status misconfigured(std::string_view why) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                       composition::kCompositionDomain,
                                                       code(composition::CompositionError::BadConfiguration),
                                                       why)
                                              .error()};
}

} // namespace

result::Status ShellParticipant::load(composition::ParticipantContext& context) {
    const composition::Configuration& configuration = context.configuration();
    const auto kGame = configuration.path("studio.game");
    if (!kGame.has_value()) {
        return misconfigured("Studio needs studio.game, the game description its session reads");
    }
    const std::filesystem::path kDescription{std::string{*kGame}};
    const std::filesystem::path kRoot{
        std::string{configuration.path("studio.root").value_or(kDescription.parent_path().string())}};
    const auto kEndpoint = configuration.text("studio.preview.endpoint");
    const auto kPin = configuration.path("studio.preview.pin_file");
    const auto kToken = configuration.path("studio.preview.token_file");
    if (kEndpoint.has_value() != kPin.has_value() || kEndpoint.has_value() != kToken.has_value()) {
        return misconfigured("a preview needs studio.preview.endpoint, pin_file, and token_file together");
    }
    if (kEndpoint.has_value()) {
        preview_ = Preview{std::string{*kEndpoint}, std::string{*kPin}, std::string{*kToken}};
    }
    // A game Studio plays itself to preview it (D445).
    const auto kPlayServer = configuration.path("studio.play.server");
    const auto kPlayClient = configuration.path("studio.play.client");
    if (kPlayServer.has_value() != kPlayClient.has_value()) {
        return misconfigured("playing needs studio.play.server and studio.play.client together");
    }
    if (kPlayServer.has_value()) {
        std::error_code error;
        play_ = PlaySettings{
            .server = std::string{*kPlayServer},
            .client = std::string{*kPlayClient},
            .game = std::filesystem::absolute(kDescription, error),
            .serverSettings = std::string{configuration.path("studio.play.server_settings").value_or("")},
            .clientSettings = std::string{configuration.path("studio.play.client_settings").value_or("")},
            // The game's own play directory, where a debugger given the game
            // finds it (D462).
            .directory = std::string{configuration.path("studio.play.directory")
                                         .value_or(authoring_session::playDirectoryOf(kDescription).string())}};
    }
    // The cook tool: named, or beside Studio, where an export puts it; the
    // content goes where the game plays (D502).
    cookTool_ = std::string{configuration.path("studio.cook").value_or(process::besideSelf("rawframe-cook").string())};
    cookAt_ = play_.has_value() ? play_->directory : authoring_session::playDirectoryOf(kDescription);
    if (play_.has_value()) {
        play_->content = cookAt_ / "content";
    }
    session_ = std::make_unique<authoring_session::Session>(kDescription, kRoot, cookTool_);
    description_ = kDescription;
    sceneRoot_ = kRoot;
    // VS Code by default; any editor that opens a file at a line by its
    // arguments (ADR-0066, D453).
    editor_ = std::string{configuration.text("studio.editor").value_or("code --goto {file}:{line}:{column}")};
    // A game that does not read is shown so, its diagnostic opened in the
    // author's editor, and read again when asked.
    static_cast<void>(openGame());
    title_ = "Rawframe Studio  " + kDescription.filename().string();
    if (context.has(view::kUiPointing.name)) {
        RAWFRAME_TRY_ASSIGN(pointing_, context.capability(view::kUiPointing));
    }
    if (context.has(view::kUiTyping.name)) {
        RAWFRAME_TRY_ASSIGN(typing_, context.capability(view::kUiTyping));
    }
    RAWFRAME_TRY_ASSIGN(tree_, ui::Tree::create());
    // Studio's own font, embedded, sanitized as the cook sanitizes a
    // game's (ADR-0049), never an unchecked font in the tree.
    const std::span<const std::byte> kFontBytes = std::as_bytes(std::span{kStudioFont});
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kSanitized, font_import::sanitize(kFontBytes));
    RAWFRAME_TRY_ASSIGN(font_, tree_->addFont(kSanitized));
    RAWFRAME_TRY(tree_->setDefaultFont(font_));
    return build();
}

bool ShellParticipant::openGame() {
    bool ended = false;
    const std::string kWelcome = session_->answer(R"({"kind":"authoring.hello","id":1,"surfaceGeneration":1})", ended);
    ++records_;
    if (kWelcome.find("\"authoring.welcome\"") == std::string::npos) {
        broken_ = answeredOf(kWelcome).message;
        diagnostic_ = diagnosticOf(kWelcome);
        return false;
    }
    broken_.reset();
    diagnostic_.reset();
    // What the session offers decides what Studio offers (ADR-0032).
    catalog_ = catalogOf(session_->answer(R"({"kind":"authoring.describe","id":2})", ended));
    ++records_;
    assets_ = assetsOf(ask(assetsRecord(next())));
    std::vector<std::pair<std::string, std::string>> scenes;
    for (const auto& [kIdentity, kPath] : authoring_session::scenesBeside(description_)) {
        std::error_code error;
        std::array<char, base::kBits128HexDigits> digits{};
        base::formatBits128Hex(kIdentity, digits);
        scenes.emplace_back(std::filesystem::relative(kPath, sceneRoot_, error).generic_string(),
                            std::string{digits.data(), digits.size()});
    }
    std::ranges::sort(scenes);
    scenes_.clear();
    sceneSources_.clear();
    for (auto& [path, source] : scenes) {
        scenes_.push_back(std::move(path));
        sceneSources_.push_back(std::move(source));
    }
    return true;
}

result::Status ShellParticipant::start(composition::ParticipantContext& context) noexcept {
    emitter_ = context.emitter();
    // A press is acted on as the window's records come, so the keys
    // that follow it find the keyboard where it put it (D426): a field
    // clicked takes what is typed next, however slowly frames come.
    if (pointing_ != nullptr) {
        pointing_->onPress([this](float x, float y) {
            pressAt(x, y);
        });
    }
    if (pointing_ != nullptr) {
        pointing_->onWheel([this](float x, float y, float deltaX, float deltaY) {
            wheels_.push_back({x, y, deltaX, deltaY});
        });
    }
    if (typing_ != nullptr) {
        typing_->answer([this](const view::Typing& typing) {
            typed_.push_back(typing);
        });
    }
    return {};
}

void ShellParticipant::runHostPhase(composition::HostPhase, const composition::HostFrame&) noexcept {
    drawn_ = nullptr;
    for (const view::Typing& kTyping : typed_) {
        take(kTyping);
    }
    typed_.clear();
    // Scroll steps ease over the layouts that follow, by this clock.
    const double kSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began_).count() + 1;
    hearCook();
    attachPlayed(kSeconds);
    pollPicks(kSeconds);
    // A press is acted on as the window's records come and may have
    // changed what a column holds; the wheel is turned over it laid out,
    // so a click and a turn read in one frame scroll what the click
    // brought, not only what was shown before it (D479).
    if (!wheels_.empty() &&
        !tree_->layOut(root_, static_cast<float>(width_), static_cast<float>(height_), kSeconds).has_value()) {
        return;
    }
    for (const auto& [kX, kY, kDeltaX, kDeltaY] : wheels_) {
        ++wheeled_;
        static_cast<void>(tree_->wheel(root_, kX, kY, kDeltaX, kDeltaY, kSeconds));
    }
    wheels_.clear();
    if (!tree_->layOut(root_, static_cast<float>(width_), static_cast<float>(height_), kSeconds).has_value()) {
        return;
    }
    pressedSinceLayout_ = false;
    list_ = {};
    if (tree_->draw(root_, 1.0F, list_).has_value()) {
        if (edit_ != nullptr) {
            static_cast<void>(edit_->decorate(root_, list_));
        }
        drawn_ = &list_;
        if (framesDrawn_++ == 0) {
            emitter_.log(diagnostics::Severity::Info, kShown, "Studio is shown");
        }
    }
}

void ShellParticipant::stop() noexcept {
    if (pointing_ != nullptr) {
        pointing_->onPress({});
        pointing_->onWheel({});
    }
    if (typing_ != nullptr) {
        typing_->focus(std::nullopt);
        typing_->answer({});
    }
    if (playing_.has_value()) {
        playing_->stop();
    }
    // Ending lets a preview go, the player's camera given back (D433).
    bool ended = false;
    static_cast<void>(session_->answer(R"({"kind":"authoring.end","id":0})", ended));
    emitter_.log(diagnostics::Severity::Info,
                 kSummary,
                 "what Studio showed",
                 {diagnostics::field("records", records_),
                  diagnostics::field("scenes", static_cast<std::uint64_t>(scenes_.size())),
                  diagnostics::field("scene", std::string_view{scene_}),
                  diagnostics::field("entities", static_cast<std::uint64_t>(entities_.size())),
                  diagnostics::field("entity", std::string_view{entity_}),
                  diagnostics::field("components", components_),
                  diagnostics::field("applied", applied_),
                  diagnostics::field("refused", refused_),
                  diagnostics::field("undone", undone_),
                  diagnostics::field("redone", redone_),
                  diagnostics::field("undoable", static_cast<std::uint64_t>(undoable_)),
                  diagnostics::field("redoable", static_cast<std::uint64_t>(redoable_)),
                  diagnostics::field("viewsSet", viewsSet_),
                  diagnostics::field("viewsMoved", viewsMoved_),
                  diagnostics::field("snapped", snapped_),
                  diagnostics::field("searched", searched_),
                  diagnostics::field("wheeled", wheeled_),
                  diagnostics::field("componentsScrolled", static_cast<double>(tree_->scrollOf(componentsColumn_)[1])),
                  diagnostics::field("previewing", previewing_),
                  diagnostics::field("played", played_),
                  diagnostics::field("playing", playing_.has_value() && playing_->running()),
                  diagnostics::field("opened", opened_),
                  diagnostics::field("picked", picked_),
                  diagnostics::field("dragged", dragged_),
                  diagnostics::field("raised", raised_),
                  diagnostics::field("turned", turned_),
                  diagnostics::field("marked", marked_),
                  diagnostics::field("status", std::string_view{status_}),
                  diagnostics::field("framesDrawn", framesDrawn_),
                  diagnostics::field("boxes", static_cast<std::uint64_t>(list_.boxes.size())),
                  diagnostics::field("glyphs", static_cast<std::uint64_t>(list_.glyphs.size()))});
}

composition::CapabilityObject ShellParticipant::provide(std::string_view capability) noexcept {
    if (capability == ui::kUiFrames.name) {
        return composition::provideAs<ui::UiFrames>(*this);
    }
    return {};
}

const ui::DrawList* ShellParticipant::drawn() const noexcept {
    return drawn_;
}

void ShellParticipant::resize(std::uint32_t width, std::uint32_t height) noexcept {
    if (width != 0 && height != 0) {
        width_ = width;
        height_ = height;
    }
}

std::shared_ptr<const texture::Texture> ShellParticipant::image(std::uint64_t) const {
    return nullptr;
}

namespace {

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
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "studio.shell",
        .budgetOwner = "ui",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
}
} // namespace rawframe::studio
