#include "generated/studio_font.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/document/json.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/studio/registrar.h"
#include "rawframe/ui/frames.h"
#include "rawframe/ui/tree.h"
#include "rawframe/view/pointing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

namespace {

constexpr diagnostics::EventIdentity kSummary{"studio", "studio_summary"};
constexpr diagnostics::EventIdentity kShown{"studio", "studio_shown"};
constexpr std::string_view kProvided[] = {ui::kUiFrames.name};
constexpr std::string_view kMaybe[] = {view::kUiPointing.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

/// Studio's colors, 0xRRGGBBAA: the window behind everything, a panel, a
/// row, and the header.
constexpr std::uint32_t kBackground = 0x1E1F24FFU;
constexpr std::uint32_t kPanel = 0x2A2C33FFU;
constexpr std::uint32_t kRow = 0x3A3D47FFU;
constexpr std::uint32_t kChosen = 0x4A6FA5FFU;
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

using document::Value;

/// A field's value as a row shows it: a number or a text as itself,
/// anything else as its compact JSON.
std::string shown(const Value& value) {
    if (value.kind() == Value::Kind::Object && value.names().size() == 1) {
        const Value& kOnly = *value.find(value.names().front());
        if (kOnly.kind() == Value::Kind::String) {
            return *kOnly.text();
        }
        return document::writeCompact(kOnly);
    }
    return document::writeCompact(value);
}

/// The first answer of a read's reply, if it is one.
std::optional<Value> firstAnswer(std::string_view reply) {
    auto parsed = document::parse(reply);
    const Value* answer = parsed.has_value() ? parsed->find("answer") : nullptr;
    const Value* answers = answer != nullptr ? answer->find("answers") : nullptr;
    if (answers == nullptr || answers->kind() != Value::Kind::Array || answers->items().empty()) {
        return std::nullopt;
    }
    const Value* first = answers->items().front().find("answer");
    return first != nullptr ? std::optional<Value>{*first} : std::nullopt;
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
        if (context.has(view::kUiPointing.name)) {
            RAWFRAME_TRY_ASSIGN(pointing_, context.capability(view::kUiPointing));
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

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        // A press is taken as the window's records come, and acted on in
        // the next presentation, between frames.
        if (pointing_ != nullptr) {
            pointing_->onPress([this](float x, float y) {
                presses_.push_back({x, y});
            });
        }
        return {};
    }

    void runHostPhase(composition::HostPhase, const composition::HostFrame&) noexcept override {
        drawn_ = nullptr;
        for (const auto& [kX, kY] : presses_) {
            pressAt(kX, kY);
        }
        presses_.clear();
        if (!tree_->layOut(root_, static_cast<float>(width_), static_cast<float>(height_)).has_value()) {
            return;
        }
        list_ = {};
        if (tree_->draw(root_, 1.0F, list_).has_value()) {
            drawn_ = &list_;
            if (framesDrawn_++ == 0) {
                emitter_.log(diagnostics::Severity::Info, kShown, "Studio is shown");
            }
        }
    }

    void stop() noexcept override {
        if (pointing_ != nullptr) {
            pointing_->onPress({});
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
        RAWFRAME_TRY_ASSIGN(scenesColumn_, column(kColumns, "Scenes"));
        RAWFRAME_TRY_ASSIGN(entitiesColumn_, column(kColumns, "Entities"));
        RAWFRAME_TRY_ASSIGN(componentsColumn_, column(kColumns, "Components"));
        for (std::size_t each = 0; each < scenes_.size(); ++each) {
            RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode, row(scenesColumn_, scenes_[each], kText));
            sceneRows_.push_back(kRowNode);
        }
        return {};
    }

    /// A row of `text` under `column`, its height fixed.
    result::Result<ui::Node>
    row(ui::Node column, std::string_view text, std::uint32_t color, std::uint32_t fill = kRow) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                            box(column, ui::Layout{.height = ui::pixels(28), .padding = {8, 4, 8, 4}}, fill));
        RAWFRAME_TRY(words(kRowNode, text, color, 14));
        return kRowNode;
    }

    /// Removes `rows` from the tree.
    void clear(std::vector<ui::Node>& rows) {
        for (const ui::Node kRowNode : rows) {
            static_cast<void>(tree_->remove(kRowNode));
        }
        rows.clear();
    }

    /// One record to the session, counted; its reply.
    std::string ask(const Value& record) {
        bool ended = false;
        ++records_;
        return session_->answer(document::writeCompact(record), ended);
    }

    /// A read of `scene`: one query, `operation`, with `entity` if given.
    std::optional<Value> read(const std::string& scene, std::string_view operation, std::string_view entity = {}) {
        Value query = Value::object();
        query.add("operation", Value::string(std::string{operation}));
        if (!entity.empty()) {
            query.add("entity", Value::string(std::string{entity}));
        }
        Value queries = Value::array();
        queries.push(std::move(query));
        Value document = Value::object();
        document.add("formatVersion", Value::integer(1));
        document.add("kind", Value::string("authoring.query"));
        document.add("queries", std::move(queries));
        Value record = Value::object();
        record.add("kind", Value::string("authoring.read"));
        record.add("id", Value::integer(static_cast<std::int64_t>(records_ + 1)));
        record.add("scene", Value::string(scene));
        record.add("queries", std::move(document));
        return firstAnswer(ask(record));
    }

    /// A press at `x`, `y`: a scene row shows its entities, an entity row
    /// chooses it and shows its components.
    void pressAt(float x, float y) {
        const auto kHit = tree_->hit(root_, x, y);
        if (!kHit.has_value() || !kHit->node.has_value()) {
            return;
        }
        // The row is the node hit or the one its words are on.
        const ui::Node kNode = *kHit->node;
        if (const auto kScene = std::ranges::find(sceneRows_, kNode); kScene != sceneRows_.end()) {
            showScene(static_cast<std::size_t>(kScene - sceneRows_.begin()));
        } else if (const auto kEntity = std::ranges::find(entityRows_, kNode); kEntity != entityRows_.end()) {
            showEntity(static_cast<std::size_t>(kEntity - entityRows_.begin()));
        }
    }

    void showScene(std::size_t at) {
        scene_ = scenes_[at];
        for (std::size_t each = 0; each < sceneRows_.size(); ++each) {
            static_cast<void>(
                tree_->setLook(sceneRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
        }
        clear(entityRows_);
        clear(componentRows_);
        entities_.clear();
        entity_.clear();
        components_ = 0;
        const std::optional<Value> kList = read(scene_, "scene.list_entities");
        const Value* listed = kList.has_value() ? kList->find("entities") : nullptr;
        if (listed == nullptr || listed->kind() != Value::Kind::Array) {
            return;
        }
        for (const Value& each : listed->items()) {
            const Value* id = each.find("id");
            const Value* name = each.find("name");
            const Value* brought = each.find("brought");
            if (id == nullptr || id->text() == nullptr) {
                continue;
            }
            std::string label = name != nullptr && name->text() != nullptr ? *name->text() : std::string{};
            if (label.empty() && brought != nullptr) {
                const Value* instance = brought->find("instance");
                label = "instance " + (instance != nullptr ? document::writeCompact(*instance) : std::string{"?"}) +
                        "  " + id->text()->substr(0, 8);
            }
            auto added = row(entitiesColumn_, label, brought != nullptr ? kQuiet : kText);
            if (!added.has_value()) {
                break;
            }
            entityRows_.push_back(*added);
            entities_.push_back(*id->text());
        }
    }

    void showEntity(std::size_t at) {
        entity_ = entities_[at];
        for (std::size_t each = 0; each < entityRows_.size(); ++each) {
            static_cast<void>(
                tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
        }
        clear(componentRows_);
        components_ = 0;
        // Chosen in the session, so undo and redo keep it (D417).
        Value entities = Value::array();
        entities.push(Value::string(entity_));
        Value select = Value::object();
        select.add("kind", Value::string("authoring.select"));
        select.add("id", Value::integer(static_cast<std::int64_t>(records_ + 1)));
        select.add("scene", Value::string(scene_));
        select.add("entities", std::move(entities));
        static_cast<void>(ask(select));
        const std::optional<Value> kRead = read(scene_, "scene.read_entity", entity_);
        const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
        if (components == nullptr || components->kind() != Value::Kind::Array) {
            return;
        }
        for (const Value& each : components->items()) {
            const Value* name = each.find("name");
            auto added =
                row(componentsColumn_, name != nullptr && name->text() != nullptr ? *name->text() : "?", kText);
            if (!added.has_value()) {
                return;
            }
            componentRows_.push_back(*added);
            ++components_;
            const Value* fields = each.find("fields");
            for (const Value& field : fields != nullptr ? fields->items() : std::span<const Value>{}) {
                const Value* fieldName = field.find("name");
                const Value* value = field.find("value");
                const std::string kLine =
                    (fieldName != nullptr && fieldName->text() != nullptr ? *fieldName->text() : std::string{}) +
                    " = " + (value != nullptr ? shown(*value) : std::string{});
                auto line = row(componentsColumn_, kLine, kQuiet, kPanel);
                if (!line.has_value()) {
                    return;
                }
                componentRows_.push_back(*line);
            }
        }
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
    view::UiPointing* pointing_ = nullptr;
    std::vector<std::pair<float, float>> presses_;
    ui::Node scenesColumn_{};
    ui::Node entitiesColumn_{};
    ui::Node componentsColumn_{};
    std::vector<ui::Node> sceneRows_;
    std::vector<ui::Node> entityRows_;
    std::vector<ui::Node> componentRows_;
    std::vector<std::string> entities_;
    std::string scene_;
    std::string entity_;
    std::uint64_t components_ = 0;
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
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "studio.shell",
        .budgetOwner = "ui",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
}

} // namespace rawframe::studio
