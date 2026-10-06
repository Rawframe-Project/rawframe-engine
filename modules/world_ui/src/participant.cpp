#include "rawframe/assets/assets.h"
#include "rawframe/composition/composition.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/game_textures/asked.h"
#include "rawframe/graph/graph.h"
#include "rawframe/ui/font.h"
#include "rawframe/view/players.h"
#include "rawframe/view/pointing.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/hover.h"
#include "rawframe/world_kest/layouts.h"
#include "rawframe/world_localization/text.h"
#include "rawframe/world_replication/client_worlds.h"
#include "rawframe/world_ui/errors.h"
#include "rawframe/world_ui/frames.h"
#include "rawframe/world_ui/registrar.h"
#include "rawframe/world_ui/world_ui.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::world_ui {

namespace {

constexpr diagnostics::EventIdentity kUiSummary{"ui", "ui_summary"};
constexpr diagnostics::EventIdentity kFailed{"ui", "ui_failed"};
constexpr diagnostics::EventIdentity kImageUnknown{"ui", "image_unknown"};
constexpr diagnostics::EventIdentity kImageUnread{"ui", "image_unavailable"};
constexpr diagnostics::EventIdentity kFontUnread{"ui", "font_unavailable"};
/// The decoded levels each image may hold.
constexpr std::uint64_t kImageBudgetBytes = std::uint64_t{64} * 1024 * 1024;
/// The fonts' bytes, all together (D386).
constexpr std::uint64_t kFontBudgetBytes = std::uint64_t{64} * 1024 * 1024;
constexpr std::string_view kProvided[] = {kUiFrames.name, world_kest::kUiHover.name};
constexpr std::string_view kMaybe[] = {world_replication::kClientWorlds.name,
                                       world_kest::kGameFiles.name,
                                       view::kPlayerViews.name,
                                       view::kUiPointing.name,
                                       view::kUiTyping.name,
                                       game_content::kGameContent.name,
                                       world_localization::kGameText.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldUiDomain, code(WorldUiError::BadNodes), why).error()};
}

/// A cooked font's bytes, as the set holds them: the sanitized font the
/// cook wrote, which the tree checks again as it reads it.
result::Result<assets::DecodedForm> decodeFont(const content::VerifiedContent& content) {
    const std::span<const std::byte> kBytes = content.bytes();
    return assets::DecodedForm{.value = std::make_shared<const std::vector<std::byte>>(kBytes.begin(), kBytes.end()),
                               .bytes = kBytes.size()};
}

/// A font a `font` line names, asked of the set.
struct WantedFont {
    std::uint64_t id = 0;
    assets::RequesterId requester;
    bool done = false;
};

/// The game's node components and their parents; none for a game without
/// any.
result::Result<std::optional<UiSettings>> settingsOf(const world_kest::GameFiles& files, const kest::Program& program) {
    const world_kest::GameDescription& kGame = files.description();
    UiSettings settings;
    std::vector<std::string_view> names;
    for (const world_kest::GameComponent& component : kGame.components) {
        if (world_kest::ofEngineType(component, "rawframe.ui.Node")) {
            settings.nodes.push_back(component.id);
            names.push_back(component.name);
        }
    }
    if (settings.nodes.empty()) {
        return std::optional<UiSettings>{};
    }
    // By its full name: a game's own type called `Node` is not this one.
    if (!world_kest::laidOutAs(program,
                               "rawframe.ui.Node",
                               sizeof(Node),
                               {{"widthScale", offsetof(Node, widthScale)},
                                {"widthOffset", offsetof(Node, widthOffset)},
                                {"heightScale", offsetof(Node, heightScale)},
                                {"heightOffset", offsetof(Node, heightOffset)},
                                {"direction", offsetof(Node, direction)},
                                {"justify", offsetof(Node, justify)},
                                {"alignItems", offsetof(Node, alignItems)},
                                {"alignSelf", offsetof(Node, alignSelf)},
                                {"gap", offsetof(Node, gap)},
                                {"grow", offsetof(Node, grow)},
                                {"shrink", offsetof(Node, shrink)},
                                {"padding", offsetof(Node, padding)},
                                {"margin", offsetof(Node, margin)},
                                {"border", offsetof(Node, border)},
                                {"absolute", offsetof(Node, absolute)},
                                {"xScale", offsetof(Node, xScale)},
                                {"xOffset", offsetof(Node, xOffset)},
                                {"yScale", offsetof(Node, yScale)},
                                {"yOffset", offsetof(Node, yOffset)},
                                {"anchorX", offsetof(Node, anchorX)},
                                {"anchorY", offsetof(Node, anchorY)},
                                {"fill", offsetof(Node, fill)},
                                {"borderColor", offsetof(Node, borderColor)},
                                {"radius", offsetof(Node, radius)},
                                {"clip", offsetof(Node, clip)},
                                {"order", offsetof(Node, order)},
                                {"image", offsetof(Node, image)},
                                {"imageTint", offsetof(Node, imageTint)},
                                {"imageSlice", offsetof(Node, imageSlice)},
                                {"shadowColor", offsetof(Node, shadowColor)},
                                {"shadowX", offsetof(Node, shadowX)},
                                {"shadowY", offsetof(Node, shadowY)},
                                {"shadowBlur", offsetof(Node, shadowBlur)},
                                {"gradientKind", offsetof(Node, gradientKind)},
                                {"gradientAngle", offsetof(Node, gradientAngle)},
                                {"gradientFrom", offsetof(Node, gradientFrom)},
                                {"gradientTo", offsetof(Node, gradientTo)},
                                {"text", offsetof(Node, text)},
                                {"textValue", offsetof(Node, textValue)},
                                {"font", offsetof(Node, font)},
                                {"textSize", offsetof(Node, textSize)},
                                {"textColor", offsetof(Node, textColor)},
                                {"textAlign", offsetof(Node, textAlign)},
                                {"textWrap", offsetof(Node, textWrap)},
                                {"press", offsetof(Node, press)},
                                {"hit", offsetof(Node, hit)},
                                {"layer", offsetof(Node, layer)},
                                {"edit", offsetof(Node, edit)},
                                {"editLimit", offsetof(Node, editLimit)}})) {
        return refuse("the game's rawframe.ui.Node is not as the engine reads it");
    }
    settings.parents.resize(settings.nodes.size());
    for (const world_kest::GameUiParent& kLine : kGame.uiParents) {
        const auto kNode = std::ranges::find(names, kLine.node);
        const auto kParent = std::ranges::find(names, kLine.parent);
        if (kNode == names.end() || kParent == names.end()) {
            return refuse("a ui line names a component that is not a rawframe.ui.Node");
        }
        settings.parents[static_cast<std::size_t>(kNode - names.begin())] =
            static_cast<std::size_t>(kParent - names.begin());
    }
    for (const world_kest::GameFont& kFont : kGame.fonts) {
        settings.fonts.push_back(kFont.id);
    }
    return std::optional<UiSettings>{std::move(settings)};
}

/// Lays out each local player's UI in its view in `presentation_extract`
/// and lends what it drew.
class UiParticipant final : public composition::Participant, public UiFrames, public world_kest::UiHover {
public:
    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(world_kest::kGameFiles.name) || !context.has(world_replication::kClientWorlds.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(const world_kest::GameFiles* files, context.capability(world_kest::kGameFiles));
        if (!files->named()) {
            return {};
        }
        // A game without node components has no UI: its program is not
        // compiled again for one.
        if (std::ranges::none_of(files->description().components, [](const world_kest::GameComponent& component) {
                return world_kest::ofEngineType(component, "rawframe.ui.Node");
            })) {
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
        RAWFRAME_TRY_ASSIGN(std::optional<UiSettings> settings, settingsOf(*files, **program));
        if (!settings.has_value()) {
            return {};
        }
        // A label's words from the game's text in the player's locale; none
        // without it.
        if (context.has(world_localization::kGameText.name)) {
            RAWFRAME_TRY_ASSIGN(const world_localization::GameText* text,
                                context.capability(world_localization::kGameText));
            settings->words = [text, labels = files->description().labels](
                                  std::uint64_t label, std::int64_t value) -> std::optional<std::string> {
                const auto kLabel = std::ranges::find(labels, label, &world_kest::GameLabel::id);
                if (kLabel == labels.end()) {
                    return std::nullopt;
                }
                const std::array<localization::Argument, 1> kArguments{
                    localization::Argument{.name = kLabel->argument, .value = value}};
                auto words = text->format(
                    kLabel->table, kLabel->key, std::span{kArguments}.first(kLabel->argument.empty() ? 0 : 1));
                return words.has_value() ? std::optional{std::move(*words)} : std::nullopt;
            };
        }
        RAWFRAME_TRY_ASSIGN(clients_, context.capability(world_replication::kClientWorlds));
        if (context.has(view::kPlayerViews.name)) {
            RAWFRAME_TRY_ASSIGN(views_, context.capability(view::kPlayerViews));
        }
        if (context.has(view::kUiPointing.name)) {
            RAWFRAME_TRY_ASSIGN(pointing_, context.capability(view::kUiPointing));
        }
        if (context.has(view::kUiTyping.name)) {
            RAWFRAME_TRY_ASSIGN(typing_, context.capability(view::kUiTyping));
        }
        RAWFRAME_TRY_ASSIGN(ui_, WorldUi::create(std::move(*settings)));
        // The players' regions as the scene and the canvas have them (D364,
        // D369): the layout for their count, else the whole window.
        const std::size_t kPlayers = std::max<std::size_t>(clients_->localPlayers(), 1);
        const auto& kLayouts = files->description().layouts;
        if (const auto kLayout = std::ranges::find(kLayouts, kPlayers, &world_kest::GameLayout::players);
            kLayout != kLayouts.end()) {
            regions_ = kLayout->regions;
        } else {
            regions_.assign(1, world_kest::GameRegion{});
        }
        aspect_ = files->description().aspect;
        // The images the UI names, each read from the game's cooked content
        // on its first naming (D378); without content, none is ever ready.
        std::optional<game_textures::TextureReading> reading;
        if (context.has(game_content::kGameContent.name) && context.cpuExecutor() != nullptr) {
            RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
            if (content->held()) {
                RAWFRAME_TRY(content->admit(game_textures::textureRepresentations()));
                reading = game_textures::TextureReading{.store = &content->store(),
                                                        .cpu = context.cpuExecutor(),
                                                        .owner = context.owner(),
                                                        .scope = &context.scope(),
                                                        .clock = &context.clock()};
                // The fonts, every one now: text waits for them (D386).
                if (!files->fonts().empty()) {
                    const content::ResourceTypeId kFontType{ui::kFontType};
                    RAWFRAME_TRY(content->admit(std::vector<content::AdmittedRepresentation>{
                        {.type = kFontType,
                         .representation = *content::RepresentationId::parse(ui::kFontRepresentation)}}));
                    RAWFRAME_TRY_ASSIGN(
                        fontSet_,
                        assets::AssetSet::create(
                            content->store(),
                            *context.cpuExecutor(),
                            context.owner(),
                            context.scope(),
                            context.clock(),
                            {.type = kFontType, .decode = &decodeFont, .budgetBytes = kFontBudgetBytes}));
                    for (const world_kest::GameFontResource& kFont : files->fonts()) {
                        RAWFRAME_TRY_ASSIGN(const assets::RequesterId kRequester,
                                            fontSet_->request(content::ResourceRef{
                                                .id = content::ResourceId{kFont.font}, .type = kFontType}));
                        fonts_.push_back(WantedFont{.id = kFont.id, .requester = kRequester});
                    }
                }
            }
        }
        images_ = game_textures::AskedTextures{reading, files->textures(), kImageBudgetBytes};
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        // Presses anywhere in the window, a mouse being every local
        // player's, against the UI as last laid out (D421).
        if (pointing_ != nullptr && ui_ != nullptr) {
            pointing_->answer([this](float x, float y, bool pressing) -> std::optional<std::int64_t> {
                if (failed_) {
                    return std::nullopt;
                }
                const std::optional<std::int64_t> kTaken = ui_->press(x, y);
                presses_ += pressing && kTaken.has_value() ? 1 : 0;
                return kTaken;
            });
        }
        // A press going down gives a text field the keyboard, or takes it,
        // as the host reads the window's records (D426): the keys after it
        // find the keyboard where it put it.
        if (pointing_ != nullptr && ui_ != nullptr) {
            pointing_->onPress([this](float x, float y) {
                if (failed_) {
                    return;
                }
                ui_->pressAt(x, y);
                if (typing_ != nullptr) {
                    typing_->focus(ui_->caret());
                }
            });
        }
        // What is typed, to the field holding the keyboard.
        if (typing_ != nullptr && ui_ != nullptr) {
            typing_->answer([this](const view::Typing& typing) {
                if (!failed_) {
                    ui_->type(typing);
                }
            });
        }
        return {};
    }

    void runHostPhase(composition::HostPhase /*phase*/, const composition::HostFrame& /*frame*/) noexcept override {
        drawn_ = nullptr;
        if (ui_ == nullptr || failed_) {
            return;
        }
        // The window in logical pixels as the host last heard it, else the
        // frame's size: device pixels each logical one.
        const float kWidth = static_cast<float>(width_);
        const float kHeight = static_cast<float>(height_);
        float scale = 1;
        if (views_ != nullptr && views_->window().width > 0) {
            scale = kWidth / views_->window().width;
        }
        takeFonts();
        laidOut_.clear();
        for (std::size_t at = 0; at < regions_.size(); ++at) {
            const world_kest::RegionPixels kWhole = world_kest::pixelsOf(regions_[at], width_, height_);
            const world_kest::RegionPixels kPixels =
                aspect_.has_value() ? world_kest::constrainedTo(kWhole, *aspect_) : kWhole;
            const std::size_t kClient = at == 0 ? clients_->playerClient().value_or(0) : at;
            const world_replication::ClientView kView = clients_->client(kClient);
            laidOut_.push_back(UiView{.world = kView.world,
                                      .player = kView.owned,
                                      .x = static_cast<float>(kPixels.x) / scale,
                                      .y = static_cast<float>(kPixels.y) / scale,
                                      .width = static_cast<float>(kPixels.width) / scale,
                                      .height = static_cast<float>(kPixels.height) / scale});
        }
        if (const result::Status kDrawn = ui_->update(laidOut_, kWidth / scale, kHeight / scale, scale);
            !kDrawn.has_value()) {
            failed_ = true;
            emitter_.log(diagnostics::Severity::Error,
                         kFailed,
                         "the UI could not be laid out: nothing more of it is drawn",
                         {diagnostics::field("reason", std::string{kDrawn.error().description()})});
            return;
        }
        drawn_ = &ui_->drawn();
        // Where the field holding the keyboard has its caret, for the
        // platform's input method, and what fields gave, for the sample.
        if (typing_ != nullptr) {
            typing_->focus(ui_->caret());
            for (view::Submitted& given : ui_->takeSubmitted()) {
                typing_->submit(std::move(given));
            }
        }
        boxes_ += drawn_->boxes.size();
        glyphRuns_ += drawn_->glyphRuns.size();
        glyphsLeftOut_ += drawn_->glyphsLeftOut;
        shadows_ += drawn_->shadows.size();
        imagesDrawn_ += drawn_->images.size();
        ++tick_;
        for (const ui::Image& kImage : drawn_->images) {
            if (const std::optional<result::Error> kNone = images_.ask(kImage.image)) {
                const bool kUnknown = kNone->errorClass() == result::ErrorClass::NotFound;
                emitter_.log(diagnostics::Severity::Warning,
                             kUnknown ? kImageUnknown : kImageUnread,
                             kUnknown ? "the UI names an image the game does not declare: it draws nothing"
                                      : "an image the UI names could not be asked for: it draws nothing",
                             {diagnostics::field("texture", graph::nodeIdText(kImage.image)),
                              diagnostics::field("reason", std::string{kNone->description()})});
            }
        }
        for (const auto& [kId, kError] : images_.update(tick_)) {
            emitter_.log(diagnostics::Severity::Warning,
                         kImageUnread,
                         "an image the UI names could not be read: it draws nothing",
                         {diagnostics::field("texture", graph::nodeIdText(kId)),
                          diagnostics::field("reason", std::string{kError.description()})});
        }
    }

    void stop() noexcept override {
        if (pointing_ != nullptr) {
            pointing_->answer({});
            pointing_->onPress({});
        }
        if (typing_ != nullptr) {
            typing_->answer({});
        }
        if (ui_ == nullptr) {
            return;
        }
        const UiStatistics& kStatistics = ui_->statistics();
        emitter_.log(diagnostics::Severity::Info,
                     kUiSummary,
                     "what the local players' UI laid out and drew",
                     {diagnostics::field("frames", kStatistics.frames),
                      diagnostics::field("made", kStatistics.made),
                      diagnostics::field("changed", kStatistics.changed),
                      diagnostics::field("leftOut", kStatistics.leftOut),
                      diagnostics::field("mostNodes", kStatistics.mostNodes),
                      diagnostics::field("boxes", boxes_),
                      diagnostics::field("images", imagesDrawn_),
                      diagnostics::field("imagesRead", images_.read()),
                      diagnostics::field("imagesReady", images_.ready()),
                      diagnostics::field("shadows", shadows_),
                      diagnostics::field("fontsRead", fontsRead_),
                      diagnostics::field("texts", kStatistics.texts),
                      diagnostics::field("textsUnknown", kStatistics.textsUnknown),
                      diagnostics::field("glyphRuns", glyphRuns_),
                      diagnostics::field("glyphsLeftOut", glyphsLeftOut_),
                      diagnostics::field("presses", presses_),
                      diagnostics::field("focused", kStatistics.focused),
                      diagnostics::field("typed", kStatistics.typed),
                      diagnostics::field("submitted", kStatistics.submitted),
                      diagnostics::field("atlasRevisions",
                                         drawn_ != nullptr && drawn_->atlas != nullptr ? drawn_->atlas->revision : 0)});
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kUiFrames.name) {
            return composition::provideAs<UiFrames>(*this);
        }
        if (capability == world_kest::kUiHover.name) {
            return composition::provideAs<world_kest::UiHover>(*this);
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

    std::shared_ptr<const texture::Texture> image(std::uint64_t id) const override {
        return images_.texture(id, tick_);
    }

    // What the host lends of the pointer, where the mouse is now (D422).
    std::int64_t hovered() const override {
        return pointing_ != nullptr ? pointing_->hovered().value_or(0) : 0;
    }

private:
    /// Fonts read since the last frame given to the UI; one that failed said
    /// once.
    void takeFonts() {
        if (fontSet_ == nullptr) {
            return;
        }
        fontSet_->update(tick_);
        for (WantedFont& wanted : fonts_) {
            if (wanted.done) {
                continue;
            }
            const assets::Readiness kReadiness = fontSet_->readiness(wanted.requester);
            std::optional<std::string> failure;
            if (kReadiness == assets::Readiness::Failed) {
                failure = std::string{fontSet_->failure(wanted.requester)->description()};
            } else if (kReadiness == assets::Readiness::Ready) {
                const std::optional<assets::AssetHandle> kHandle = fontSet_->handle(wanted.requester);
                auto bytes = kHandle.has_value()
                                 ? assets::Assets<std::vector<std::byte>>{*fontSet_}.share(*kHandle, tick_)
                                 : std::unexpected<result::Error>{result::fail(result::ErrorClass::NotFound,
                                                                               kWorldUiDomain,
                                                                               code(WorldUiError::BadNodes),
                                                                               "a font read has no handle")
                                                                      .error()};
                if (!bytes.has_value()) {
                    failure = std::string{bytes.error().description()};
                } else if (const result::Status kAdded = ui_->addFont(wanted.id, **bytes); !kAdded.has_value()) {
                    failure = std::string{kAdded.error().description()};
                } else {
                    ++fontsRead_;
                }
            } else {
                continue;
            }
            wanted.done = true;
            if (failure.has_value()) {
                emitter_.log(diagnostics::Severity::Warning,
                             kFontUnread,
                             "a font the game names could not be read: its text shows in another",
                             {diagnostics::field("font", graph::nodeIdText(wanted.id)),
                              diagnostics::field("reason", std::move(*failure))});
            }
        }
    }

    world_replication::ClientWorlds* clients_ = nullptr;
    view::PlayerViews* views_ = nullptr;
    view::UiPointing* pointing_ = nullptr;
    view::UiTyping* typing_ = nullptr;
    std::unique_ptr<WorldUi> ui_;
    std::vector<world_kest::GameRegion> regions_;
    std::optional<world_kest::GameAspect> aspect_;
    std::vector<UiView> laidOut_;
    const ui::DrawList* drawn_ = nullptr;
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 720;
    std::uint64_t boxes_ = 0;
    std::uint64_t imagesDrawn_ = 0;
    std::uint64_t shadows_ = 0;
    game_textures::AskedTextures images_{std::nullopt, {}, 0};
    std::unique_ptr<assets::AssetSet> fontSet_;
    std::vector<WantedFont> fonts_;
    std::uint64_t fontsRead_ = 0;
    std::uint64_t glyphRuns_ = 0;
    std::uint64_t glyphsLeftOut_ = 0;
    /// Presses the UI took (D421).
    std::uint64_t presses_ = 0;
    std::uint64_t tick_ = 0;
    bool failed_ = false;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<UiParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_ui.ui",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvided,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        // Images are decoded, and fonts copied, on the CPU executor;
        // stopping only says what was drawn.
        .executor = {.cpu = true, .quota = {.maximumPendingTasks = 64}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "world_ui.ui",
        .budgetOwner = "ui",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
}

} // namespace rawframe::world_ui
