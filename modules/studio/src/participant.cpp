#include "generated/studio_font.h"
#include "rawframe/authoring_session/attach.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/material/instance.h"
#include "rawframe/process/self.h"
#include "shell.h"

#include <cstdio>

namespace rawframe::studio {

namespace {

constexpr std::string_view kProvided[] = {ui::kUiFrames.name};
constexpr std::string_view kMaybe[] = {view::kUiPointing.name, view::kUiTyping.name, ui::kAccessSeat.name};
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
    // Without a game, Studio starts on its start screen (D781).
    const auto kGame = configuration.path("studio.game");
    const auto kEndpoint = configuration.text("studio.preview.endpoint");
    const auto kPin = configuration.path("studio.preview.pin_file");
    const auto kToken = configuration.path("studio.preview.token_file");
    if (kEndpoint.has_value() != kPin.has_value() || kEndpoint.has_value() != kToken.has_value()) {
        return misconfigured("a preview needs studio.preview.endpoint, pin_file, and token_file together");
    }
    if (kEndpoint.has_value() && !kGame.has_value()) {
        return misconfigured("a preview is of the game studio.game names");
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
        play_ =
            PlaySettings{.server = std::string{*kPlayServer},
                         .client = std::string{*kPlayClient},
                         .serverSettings = std::string{configuration.path("studio.play.server_settings").value_or("")},
                         .clientSettings = std::string{configuration.path("studio.play.client_settings").value_or("")}};
        playDirectory_ = std::string{configuration.path("studio.play.directory").value_or("")};
    }
    // The cook tool: named, or beside Studio, where an export puts it; the
    // content goes where the game plays (D502).
    cookTool_ = std::string{configuration.path("studio.cook").value_or(process::besideSelf("rawframe-cook").string())};
    // The templates a new game is made from, and where new games go:
    // named, or beside Studio, where its package puts them (D781).
    templates_ =
        std::string{configuration.path("studio.templates").value_or(process::besideSelf("templates").string())};
    projects_ = std::string{configuration.path("studio.projects").value_or(process::besideSelf("games").string())};
    // Studio's own preferences, the docks' sizes and the games opened last,
    // where `studio.preferences` keeps them (D779); a file that does not
    // read leaves the defaults.
    if (const auto kKept = configuration.path("studio.preferences"); kKept.has_value()) {
        preferencesFile_ = *kKept;
        if (std::FILE* file = std::fopen(preferencesFile_.string().c_str(), "rb")) {
            std::string kept(64 * 1024, '\0');
            kept.resize(std::fread(kept.data(), 1, kept.size(), file));
            std::fclose(file);
            preferences_ = preferencesOf(kept).value_or(Preferences{});
        }
    }
    // VS Code by default; any editor that opens a file at a line by its
    // arguments (ADR-0066, D453).
    configuredEditor_ = std::string{configuration.text("studio.editor").value_or("code --goto {file}:{line}:{column}")};
    // The author's own choice in the preferences comes first (D782).
    editor_ = preferences_.editor.empty() ? configuredEditor_ : preferences_.editor;
    title_ = "Rawframe Studio";
    if (kGame.has_value()) {
        const std::filesystem::path kDescription{std::string{*kGame}};
        settle(kDescription,
               std::string{configuration.path("studio.root").value_or(kDescription.parent_path().string())});
    }
    if (context.has(view::kUiPointing.name)) {
        RAWFRAME_TRY_ASSIGN(pointing_, context.capability(view::kUiPointing));
    }
    if (context.has(view::kUiTyping.name)) {
        RAWFRAME_TRY_ASSIGN(typing_, context.capability(view::kUiTyping));
    }
    if (context.has(ui::kAccessSeat.name)) {
        RAWFRAME_TRY_ASSIGN(seat_, context.capability(ui::kAccessSeat));
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

void ShellParticipant::settle(const std::filesystem::path& description, const std::filesystem::path& root) {
    std::error_code error;
    if (play_.has_value()) {
        play_->game = std::filesystem::absolute(description, error);
        // The game's own play directory, where a debugger given the game
        // finds it (D462).
        play_->directory = playDirectory_.empty() ? authoring_session::playDirectoryOf(description) : playDirectory_;
    }
    cookAt_ = play_.has_value() ? play_->directory : authoring_session::playDirectoryOf(description);
    if (play_.has_value()) {
        play_->content = cookAt_ / "content";
    }
    session_ = std::make_unique<authoring_session::Session>(description, root, cookTool_);
    description_ = description;
    sceneRoot_ = root;
    // A game that does not read is shown so, its diagnostic opened in the
    // author's editor, and read again when asked.
    static_cast<void>(openGame());
    title_ = "Rawframe Studio  " + description.filename().string();
    // Kept by its whole path, once however it was named before.
    const auto kWhole = [](const std::filesystem::path& path) {
        std::error_code ignored;
        return std::filesystem::absolute(path, ignored).lexically_normal().generic_string();
    };
    const std::string kKept = kWhole(description);
    std::erase_if(preferences_.recent, [&](const std::string& each) {
        return kWhole(each) == kKept;
    });
    preferences_ = opened(preferences_, kKept);
    keepPreferences();
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
    listed_.clear();
    for (auto& [path, source] : scenes) {
        scenes_.push_back(std::move(path));
        sceneSources_.push_back(std::move(source));
        listed_.push_back(Listed::Scene);
    }
    // The game's style sheets after its scenes, then its UI documents,
    // edited as they are (D637, D643), then its materials, instances
    // (D722) and graphs (D730), each a kind of its own.
    for (const auto& [kKind, kExtension] : {std::pair{Listed::Sheet, ".styles"},
                                            std::pair{Listed::Ui, ".ui"},
                                            std::pair{Listed::Material, ".rfmaterial"}}) {
        for (const std::filesystem::path& kFile : authoring_session::filesBeside(description_, kExtension)) {
            const bool kGraph =
                kKind == Listed::Material && !material::namesBase(authoring_session::readFile(kFile).value_or(""));
            std::error_code error;
            scenes_.push_back(std::filesystem::relative(kFile, sceneRoot_, error).generic_string());
            sceneSources_.emplace_back();
            listed_.push_back(kGraph ? Listed::Graph : kKind);
        }
    }
    return true;
}

result::Status ShellParticipant::start(composition::ParticipantContext& context) noexcept {
    emitter_ = context.emitter();
    // The window where assistive technology reads it, when its program
    // asked for it (D575); a machine whose platform cannot be reached is
    // shown unread.
    if (seat_ != nullptr) {
        if (const result::Status kSeated = seat_->seat(*tree_, root_); !kSeated.has_value()) {
            emitter_.log(diagnostics::Severity::Info,
                         kInaccessible,
                         "assistive technology cannot read Studio",
                         {diagnostics::field("reason", std::string{kSeated.error().description()})});
        } else if (seat_->access() != nullptr) {
            emitter_.log(diagnostics::Severity::Info, kAccessible, "assistive technology reads Studio", {});
        }
    }
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
        // A splitter held is let go with the press (D779).
        pointing_->onRelease([this] {
            released_ = true;
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
    // A screen reader's press is a pointer's on the node it names (D575);
    // Studio moves no focus of its own, so one it asks for is not taken. Its
    // edit of a field is typing into it, the field begun as a press begins
    // it, and given by Enter as typing's is (D613).
    if (seat_ != nullptr) {
        for (const ui::AccessRequest& kRequest : seat_->takeRequests()) {
            if (!tree_->contains(kRequest.node) || kRequest.kind == ui::AccessRequest::Kind::Focus) {
                continue;
            }
            if (kRequest.kind == ui::AccessRequest::Kind::Press || edit_ == nullptr || edit_->node() != kRequest.node) {
                const bool kMoving = beforePress();
                pressOn(kRequest.node, kMoving);
            }
            if (kRequest.kind != ui::AccessRequest::Kind::Press && edit_ != nullptr && edit_->node() == kRequest.node &&
                edit_->select(kRequest.anchor, kRequest.focus).has_value() &&
                kRequest.kind == ui::AccessRequest::Kind::Replace) {
                static_cast<void>(edit_->type(kRequest.text));
            }
        }
    }
    for (const view::Typing& kTyping : typed_) {
        take(kTyping);
    }
    typed_.clear();
    // Scroll steps ease over the layouts that follow, by this clock.
    const double kSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began_).count() + 1;
    hearCook();
    attachPlayed(kSeconds);
    pollPicks(kSeconds);
    readLogs(kSeconds);
    followSplitter();
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
    if (seat_ != nullptr) {
        seat_->leave();
    }
    if (pointing_ != nullptr) {
        pointing_->onPress({});
        pointing_->onWheel({});
        pointing_->onRelease({});
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
    if (session_ != nullptr) {
        static_cast<void>(session_->answer(R"({"kind":"authoring.end","id":0})", ended));
    }
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
                  diagnostics::field("consoleLines", consoleLines_),
                  diagnostics::field("marked", marked_),
                  diagnostics::field("status", std::string_view{status_}),
                  diagnostics::field("framesDrawn", framesDrawn_),
                  diagnostics::field("boxes", static_cast<std::uint64_t>(list_.boxes.size())),
                  diagnostics::field("glyphs", static_cast<std::uint64_t>(list_.glyphs.size()))});
    // Apart from the summary, which holds as many fields as a record may
    // (D361): how the docks were left (D779).
    emitter_.log(diagnostics::Severity::Info,
                 kLayout,
                 "how Studio's docks were left",
                 {diagnostics::field("resized", resized_),
                  diagnostics::field("hierarchyWidth", static_cast<std::int64_t>(preferences_.hierarchyWidth)),
                  diagnostics::field("inspectorWidth", static_cast<std::int64_t>(preferences_.inspectorWidth)),
                  diagnostics::field("dockHeight", static_cast<std::int64_t>(preferences_.dockHeight))});
    // How many entities were chosen at the end, and the edits made to
    // several at once (D787).
    emitter_.log(diagnostics::Severity::Info,
                 kSelection,
                 "what Studio's selection was",
                 {diagnostics::field("chosen", static_cast<std::uint64_t>(selected_.size())),
                  diagnostics::field("together", together_)});
    // And what the start screen did (D781): the games made from templates,
    // the game Studio ended on, if any, and the games kept as opened last.
    emitter_.log(diagnostics::Severity::Info,
                 kStartScreen,
                 "what Studio's start screen did",
                 {diagnostics::field("made", made_),
                  diagnostics::field("game", session_ != nullptr ? description_.filename().string() : std::string{}),
                  diagnostics::field("recent", static_cast<std::uint64_t>(preferences_.recent.size()))});
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

// Studio is shown in its window alone, never on a headset's panel.
void ShellParticipant::resize(std::uint32_t width, std::uint32_t height, bool /*panel*/) noexcept {
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
