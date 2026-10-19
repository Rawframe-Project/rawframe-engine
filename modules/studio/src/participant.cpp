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
constexpr std::string_view kMaybe[] = {view::kUiPointing.name,
                                       view::kUiTyping.name,
                                       ui::kAccessSeat.name,
                                       world_tooling::kForwarding.name,
                                       view::kFilesDropped.name};
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
    // A shared session's host joined in place of a session of Studio's own
    // (D882); the game is the author's own copy, as each collaborator has.
    const auto kJoin = configuration.text("studio.join.endpoint");
    const auto kJoinPin = configuration.path("studio.join.pin_file");
    const auto kJoinToken = configuration.path("studio.join.token_file");
    if (kJoin.has_value() != kJoinPin.has_value() || kJoin.has_value() != kJoinToken.has_value()) {
        return misconfigured("joining needs studio.join.endpoint, pin_file, and token_file together");
    }
    if (kJoin.has_value() && !kGame.has_value()) {
        return misconfigured("a host joined is of the game studio.game names");
    }
    if (kJoin.has_value()) {
        held_.joinEndpoint = std::string{*kJoin};
        held_.joinPin = std::string{*kJoinPin};
        held_.joinToken = std::string{*kJoinToken};
    }
    // A game Studio plays itself to preview it (D445).
    const auto kConfiguredServer = configuration.path("studio.play.server");
    const auto kConfiguredClient = configuration.path("studio.play.client");
    if (kConfiguredServer.has_value() != kConfiguredClient.has_value()) {
        return misconfigured("playing needs studio.play.server and studio.play.client together");
    }
    // Named, or beside Studio, where its package puts them, so Studio
    // opened by itself plays (D912).
    std::optional<std::string> kPlayServer;
    std::optional<std::string> kPlayClient;
    if (kConfiguredServer.has_value()) {
        kPlayServer = std::string{*kConfiguredServer};
        kPlayClient = std::string{*kConfiguredClient};
    } else if (const std::filesystem::path kBesideServer = process::besideSelf("rawframe-server"),
               kBesideClient = process::besideSelf("rawframe-client");
               !kBesideServer.empty() && !kBesideClient.empty()) {
        kPlayServer = kBesideServer.string();
        kPlayClient = kBesideClient.string();
    }
    if (kPlayServer.has_value()) {
        play_ =
            PlaySettings{.server = *kPlayServer,
                         .client = *kPlayClient,
                         .serverSettings = std::string{configuration.path("studio.play.server_settings").value_or("")},
                         .clientSettings = std::string{configuration.path("studio.play.client_settings").value_or("")}};
        playDirectory_ = std::string{configuration.path("studio.play.directory").value_or("")};
        // Played in the viewport, or in the client's own window (D789),
        // the viewport by default now its input reaches the game (D790).
        RAWFRAME_TRY_ASSIGN(inViewport_.inViewport, configuration.truth("studio.play_in_viewport", true));
    }
    // The scene being edited shown in the viewport, played edited (D912).
    RAWFRAME_TRY_ASSIGN(editView_, configuration.truth("studio.edit_view", true));
    // The cook tool: named, or beside Studio, where an export puts it; the
    // content goes where the game plays (D502).
    cookTool_ = std::string{configuration.path("studio.cook").value_or(process::besideSelf("rawframe-cook").string())};
    // And the tool that draws a mesh's thumbnail (D899), the same way.
    thumbnailTool_ = std::string{
        configuration.path("studio.thumbnail").value_or(process::besideSelf("rawframe-thumbnail").string())};
    exportTool_ =
        std::string{configuration.path("studio.export").value_or(process::besideSelf("rawframe-export").string())};
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
    // What lands on the viewport, for the game played there (D790).
    if (context.has(world_tooling::kForwarding.name)) {
        RAWFRAME_TRY_ASSIGN(inViewport_.forwarding, context.capability(world_tooling::kForwarding));
    }
    if (context.has(view::kUiTyping.name)) {
        RAWFRAME_TRY_ASSIGN(typing_, context.capability(view::kUiTyping));
    }
    // Files dropped on the window from outside (D802).
    if (context.has(view::kFilesDropped.name)) {
        RAWFRAME_TRY_ASSIGN(dropped_, context.capability(view::kFilesDropped));
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
    if (held_.joinEndpoint.empty()) {
        session_ = std::make_unique<authoring_session::Session>(description, root, cookTool_, thumbnailTool_);
    }
    description_ = description;
    sceneRoot_ = root;
    // A game that does not read is shown so, its diagnostic opened in the
    // author's editor, and read again when asked.
    static_cast<void>(openGame());
    title_ = "Rawframe Studio  " + description.filename().string() +
             (held_.joinedAs.empty() ? std::string{} : "  joined as " + held_.joinedAs);
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
    std::string welcome;
    if (!held_.joinEndpoint.empty() && held_.joined != nullptr) {
        welcome = held_.welcome;
    } else if (!held_.joinEndpoint.empty()) {
        // The host's welcome is the session's: the link says hello with
        // the token (D882).
        held_.joined = authoring_session::JoinedSession::open(
            held_.joinEndpoint, held_.joinPin.c_str(), held_.joinToken.c_str(), welcome);
        if (held_.joined == nullptr) {
            say("the host could not be joined: " + (welcome.starts_with('{') ? answeredOf(welcome).message : welcome));
            return false;
        }
        held_.welcome = welcome;
        const auto kRead = document::parse(welcome);
        const Value* kAnswer = kRead.has_value() ? kRead->find("answer") : nullptr;
        const Value* kAuthor = kAnswer != nullptr ? kAnswer->find("author") : nullptr;
        held_.joinedAs = kAuthor != nullptr && kAuthor->kind() == Value::Kind::String ? *kAuthor->text() : "";
    } else {
        welcome = answerLine(R"({"kind":"authoring.hello","id":1,"surfaceGeneration":1})");
    }
    ++records_;
    if (welcome.find("\"authoring.welcome\"") == std::string::npos) {
        broken_ = answeredOf(welcome).message;
        diagnostic_ = diagnosticOf(welcome);
        return false;
    }
    broken_.reset();
    diagnostic_.reset();
    // What the session offers decides what Studio offers (ADR-0032).
    catalog_ = catalogOf(answerLine(R"({"kind":"authoring.describe","id":2})"));
    ++records_;
    // The assets fields pick from, and the browser's sources (D799).
    const std::string kAssets = ask(assetsRecord(next()));
    assets_ = assetsOf(kAssets);
    browser_.sources = sourcesOf(kAssets);
    // The game's extensions, their commands Studio's own (D874).
    extensions_ = extensionsOf(ask(extensionsRecord(next())));
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
    // (D722) and graphs (D730), each a kind of its own, then its data
    // documents, edited value by value (D812), then its blockouts, edited
    // solid by solid (D850), terrains, brushed (D856), tilemaps, painted
    // (D861), scatters and sequences, line by line (D866, D869), then its
    // frames' captures, read only (D843).
    for (const auto& [kKind, kExtension] : {std::pair{Listed::Sheet, ".styles"},
                                            std::pair{Listed::Ui, ".ui"},
                                            std::pair{Listed::Material, ".rfmaterial"},
                                            std::pair{Listed::Data, ".actions"},
                                            std::pair{Listed::Data, ".mixer"},
                                            std::pair{Listed::Data, ".strings"},
                                            std::pair{Listed::Data, ".translations"},
                                            std::pair{Listed::Data, ".rfanim"},
                                            std::pair{Listed::Blockout, ".blockout"},
                                            std::pair{Listed::Terrain, ".terrain"},
                                            std::pair{Listed::Tilemap, ".tilemap"},
                                            std::pair{Listed::Scatter, ".scatter"},
                                            std::pair{Listed::Sequence, ".sequence"},
                                            std::pair{Listed::Capture, ".rfframe"},
                                            std::pair{Listed::Capture, ".rfprofile"}}) {
        for (const std::filesystem::path& kFile : authoring_session::filesBeside(description_, kExtension)) {
            const bool kGraph =
                kKind == Listed::Material && !material::namesBase(authoring_session::readFile(kFile).value_or(""));
            std::error_code error;
            scenes_.push_back(std::filesystem::relative(kFile, sceneRoot_, error).generic_string());
            sceneSources_.emplace_back();
            listed_.push_back(kGraph ? Listed::Graph : kKind);
        }
    }
    // Last, the game's own description, its settings (D818), where it is
    // among the documents.
    std::error_code error;
    const std::string kSettings = std::filesystem::relative(description_, sceneRoot_, error).generic_string();
    if (!error && !kSettings.empty() && !kSettings.starts_with("..")) {
        scenes_.push_back(kSettings);
        sceneSources_.emplace_back();
        listed_.push_back(Listed::Settings);
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
        // Where it was let go is read as it is told: a loaded Studio's next
        // frame may come after the pointer has gone on to the next press.
        pointing_->onRelease([this] {
            released_ = true;
            releasedAt_ = pointing_->pointer();
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
    hearHost();
    keepEditing();
    attachPlayed(kSeconds);
    outlineChosen();
    pollPicks(kSeconds);
    readLogs(kSeconds);
    readNetwork(kSeconds);
    readProfiler(kSeconds);
    readFrame(kSeconds);
    pollGit();
    pollExport();
    pollThumbnails(kSeconds);
    if (playing_.has_value()) {
        inViewport_.othersStarted = playing_->others();
    }
    readLive(kSeconds);
    followPicture();
    forwardInput();
    takeDrops();
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
    // A splitter dragged as Studio ends is let go where it stands, its
    // sizes kept: a loaded Studio could end before the frame that read
    // the release (D830).
    releaseSplitter();
    // Git and an export ended, and the directory their output went to
    // taken away (D871, D872).
    held_.git.reset();
    held_.exporting.reset();
    if (!held_.own.empty()) {
        std::error_code error;
        std::filesystem::remove_all(held_.own, error);
    }
    if (typing_ != nullptr) {
        typing_->focus(std::nullopt);
        typing_->answer({});
    }
    if (playing_.has_value()) {
        playing_->stop();
    }
    // Ending lets a preview go, the player's camera given back (D433).
    // A host joined is told the author leaves as its link goes.
    bool ended = false;
    if (session_ != nullptr) {
        static_cast<void>(session_->answer(R"({"kind":"authoring.end","id":0})", ended));
    }
    held_.joined.reset();
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
                  diagnostics::field("together", together_),
                  diagnostics::field("layersChosen", layersChosen_),
                  diagnostics::field("locks", locks_),
                  diagnostics::field("lockRefused", lockRefused_),
                  diagnostics::field("hiddenTold", shown_.layersHidden)});
    // What the viewport showed (D789), and how it drew (D794).
    emitter_.log(
        diagnostics::Severity::Info,
        kViewported,
        "what Studio's viewport showed",
        {diagnostics::field("inViewport", inViewport_.inViewport),
         diagnostics::field("pictures", inViewport_.picturesShown),
         diagnostics::field("inputs", inViewport_.inputsForwarded),
         diagnostics::field("inputsTaken", inViewport_.inputsTaken),
         diagnostics::field("width",
                            inViewport_.picture != nullptr ? std::uint64_t{inViewport_.picture->levels[0].width} : 0),
         diagnostics::field("height",
                            inViewport_.picture != nullptr ? std::uint64_t{inViewport_.picture->levels[0].height} : 0),
         diagnostics::field("mode", std::string_view{shown_.viewMode}),
         diagnostics::field("modesShown", shown_.modesShown),
         diagnostics::field("bookmarksKept", shown_.bookmarksKept),
         diagnostics::field("bookmarksVisited", shown_.bookmarksVisited),
         diagnostics::field("views", static_cast<std::uint64_t>(shown_.views)),
         diagnostics::field("splitsShown", shown_.splitsShown),
         diagnostics::field("overlays", static_cast<std::uint64_t>(shown_.overlays.size())),
         diagnostics::field("overlaysShown", shown_.overlaysShown)});
    // How the game was played: its players (D804).
    emitter_.log(diagnostics::Severity::Info,
                 kPlayedRecord,
                 "how Studio played the game",
                 {diagnostics::field("players", std::uint64_t{players_}),
                  diagnostics::field("othersStarted", inViewport_.othersStarted),
                  diagnostics::field("paused", inViewport_.paused),
                  diagnostics::field("pauses", inViewport_.pauses),
                  diagnostics::field("steps", inViewport_.steps),
                  diagnostics::field("tick", inViewport_.tick),
                  diagnostics::field("ranPaused", inViewport_.ranPaused),
                  diagnostics::field("link", kLinks[network_.link].name),
                  diagnostics::field("networkReads", network_.reads),
                  diagnostics::field("mostConnections", network_.mostConnections),
                  diagnostics::field("connectionLines", network_.mostConnectionLines),
                  diagnostics::field("deviceWidth", device_.has_value() ? std::uint64_t{(*device_)[0]} : 0),
                  diagnostics::field("live", live_.on),
                  diagnostics::field("liveListed", live_.listed),
                  diagnostics::field("liveReads", live_.reads),
                  diagnostics::field("liveComponents", live_.components)});
    // And what the asset browser showed (D799).
    emitter_.log(diagnostics::Severity::Info,
                 kBrowsed,
                 "what Studio's asset browser showed",
                 {diagnostics::field("sources", static_cast<std::uint64_t>(browser_.sources.size())),
                  diagnostics::field("sourcesChosen", browser_.chosenCount),
                  diagnostics::field("narrowed", browser_.narrowed),
                  diagnostics::field("chosen", std::string_view{browser_.chosen}),
                  diagnostics::field("settingsSet", browser_.settingsSet),
                  diagnostics::field("settingsRefused", browser_.settingsRefused),
                  diagnostics::field("thumbnail", std::uint64_t{browser_.thumbnail}),
                  diagnostics::field("thumbnailsMade", browser_.thumbnailsMade),
                  diagnostics::field("referencesShown", browser_.referencesShown),
                  diagnostics::field("dropped", browser_.dropped),
                  diagnostics::field("droppedImported", browser_.droppedImported)});
    // And what the start screen did (D781): the games made from templates,
    // the game Studio ended on, if any, and the games kept as opened last.
    emitter_.log(diagnostics::Severity::Info,
                 kStartScreen,
                 "what Studio's start screen did",
                 {diagnostics::field("made", made_),
                  diagnostics::field("game", gameOpen() ? description_.filename().string() : std::string{}),
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

std::shared_ptr<const texture::Texture> ShellParticipant::image(std::uint64_t id) const {
    // The played game's picture, in the viewport (D789).
    if (id == kViewportImage) {
        return inViewport_.picture;
    }
    // A source's thumbnail in the browser (D801).
    const auto kPicture = browser_.pictures.find(id);
    return kPicture != browser_.pictures.end() ? kPicture->second : nullptr;
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
