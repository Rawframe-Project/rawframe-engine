#include "generated/studio_font.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/studio/registrar.h"
#include "rawframe/ui/frames.h"
#include "rawframe/ui/text_edit.h"
#include "rawframe/ui/tree.h"
#include "rawframe/view/pointing.h"
#include "rawframe/view/typing.h"
#include "records.h"

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
constexpr std::string_view kMaybe[] = {view::kUiPointing.name, view::kUiTyping.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

/// Studio's colors, 0xRRGGBBAA: the window behind everything, a panel, a
/// row, and the header.
constexpr std::uint32_t kBackground = 0x1E1F24FFU;
constexpr std::uint32_t kPanel = 0x2A2C33FFU;
constexpr std::uint32_t kRow = 0x3A3D47FFU;
constexpr std::uint32_t kChosen = 0x4A6FA5FFU;
constexpr std::uint32_t kField = 0x23252BFFU;
constexpr std::uint32_t kEditing = 0x1A1C21FFU;
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

/// A field Studio edits and what the session needs to act on it: a
/// component's field (its kind the read value's key), the chosen entity's
/// name, the name of a component to add, or a part of the scene's view.
struct FieldRow {
    enum class Role : std::uint8_t {
        Field,
        Name,
        Add,
        View
    };
    ui::Node value{};
    Role role = Role::Field;
    std::string component;
    std::string field;
    std::string kind;
};

/// A component's Remove button and the component it removes.
struct RemoveButton {
    ui::Node node{};
    std::string component;
    std::string name;
};

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
        const auto kEndpoint = configuration.text("studio.preview.endpoint");
        const auto kPin = configuration.path("studio.preview.pin_file");
        const auto kToken = configuration.path("studio.preview.token_file");
        if (kEndpoint.has_value() != kPin.has_value() || kEndpoint.has_value() != kToken.has_value()) {
            return misconfigured("a preview needs studio.preview.endpoint, pin_file, and token_file together");
        }
        if (kEndpoint.has_value()) {
            preview_ = Preview{std::string{*kEndpoint}, std::string{*kPin}, std::string{*kToken}};
        }
        session_ = std::make_unique<authoring_session::Session>(kDescription, kRoot);
        bool ended = false;
        const std::string kWelcome =
            session_->answer(R"({"kind":"authoring.hello","id":1,"surfaceGeneration":1})", ended);
        ++records_;
        if (kWelcome.find("\"authoring.welcome\"") == std::string::npos) {
            return misconfigured("Studio's session did not welcome it: the game does not read");
        }
        // What the session offers decides what Studio offers (ADR-0032).
        catalog_ = catalogOf(session_->answer(R"({"kind":"authoring.describe","id":2})", ended));
        ++records_;
        for (const auto& [kIdentity, kPath] : authoring_session::scenesBeside(kDescription)) {
            std::error_code error;
            scenes_.push_back(std::filesystem::relative(kPath, kRoot, error).generic_string());
        }
        std::ranges::sort(scenes_);
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

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        // A press is taken as the window's records come, and acted on in
        // the next presentation, between frames.
        if (pointing_ != nullptr) {
            pointing_->onPress([this](float x, float y) {
                presses_.push_back({x, y});
            });
        }
        if (typing_ != nullptr) {
            typing_->answer([this](const view::Typing& typing) {
                typed_.push_back(typing);
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
        for (const view::Typing& kTyping : typed_) {
            take(kTyping);
        }
        typed_.clear();
        if (!tree_->layOut(root_, static_cast<float>(width_), static_cast<float>(height_)).has_value()) {
            return;
        }
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

    void stop() noexcept override {
        if (pointing_ != nullptr) {
            pointing_->onPress({});
        }
        if (typing_ != nullptr) {
            typing_->focus(std::nullopt);
            typing_->answer({});
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
                      diagnostics::field("previewing", previewing_),
                      diagnostics::field("status", std::string_view{status_}),
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
        // What does not fit is cut at the panel's edge.
        RAWFRAME_TRY(tree_->setLook(kColumn, ui::Look{.fill = kPanel, .radius = 4, .clip = true}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kHeading, box(kColumn, ui::Layout{.height = ui::pixels(22)}, 0));
        RAWFRAME_TRY(words(kHeading, heading, kQuiet, 13));
        return kColumn;
    }

    /// A button reading `text` under `parent`.
    result::Result<ui::Node> button(ui::Node parent, std::string_view text) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kButton, box(parent, ui::Layout{.padding = {10, 4, 10, 4}}, kRow));
        RAWFRAME_TRY(words(kButton, text, kText, 14));
        return kButton;
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
                            box(root_,
                                ui::Layout{.height = ui::pixels(34),
                                           .justify = ui::Justify::SpaceBetween,
                                           .alignItems = ui::Align::Center,
                                           .padding = {12, 4, 6, 4}},
                                kHeaderFill));
        RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kHeader, ui::Layout{.padding = {0, 2, 0, 2}}, 0));
        RAWFRAME_TRY(words(kTitle, title_, kText));
        // The status, then undo and redo, at the header's end.
        RAWFRAME_TRY_ASSIGN(const ui::Node kEnd,
                            box(kHeader, ui::Layout{.alignItems = ui::Align::Center, .gap = 6}, 0));
        RAWFRAME_TRY_ASSIGN(statusNode_, box(kEnd, ui::Layout{.padding = {0, 2, 8, 2}}, 0));
        RAWFRAME_TRY_ASSIGN(undoNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
        RAWFRAME_TRY_ASSIGN(redoNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
        RAWFRAME_TRY(showHistory());
        RAWFRAME_TRY_ASSIGN(const ui::Node kColumns,
                            box(root_, ui::Layout{.direction = ui::Direction::Row, .gap = 6, .grow = 1}, kBackground));
        RAWFRAME_TRY_ASSIGN(scenesColumn_, column(kColumns, "Scenes"));
        RAWFRAME_TRY_ASSIGN(entitiesColumn_, column(kColumns, "Entities"));
        RAWFRAME_TRY_ASSIGN(componentsColumn_, column(kColumns, "Components"));
        // The entities' operations above their rows.
        if (catalog_.offers("scene.create_entity") || catalog_.offers("scene.destroy_entity")) {
            RAWFRAME_TRY_ASSIGN(const ui::Node kTools,
                                box(entitiesColumn_, ui::Layout{.height = ui::pixels(28), .gap = 6}, 0));
            if (catalog_.offers("scene.create_entity")) {
                RAWFRAME_TRY_ASSIGN(newNode_, button(kTools, "New"));
            }
            if (catalog_.offers("scene.destroy_entity")) {
                RAWFRAME_TRY_ASSIGN(deleteNode_, button(kTools, "Delete"));
            }
        }
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

    /// The next record's id.
    std::int64_t next() const noexcept {
        return static_cast<std::int64_t>(records_ + 1);
    }

    /// A read of `scene`: one query, `operation`, with `entity` if given.
    std::optional<Value> read(const std::string& scene, std::string_view operation, std::string_view entity = {}) {
        return firstAnswer(ask(readRecord(next(), scene, operation, entity)));
    }

    /// A press at `x`, `y`: a scene row shows its entities, an entity row
    /// chooses it and shows its components, a field takes the keyboard, and
    /// a button does its operation.
    void pressAt(float x, float y) {
        const auto kHit = tree_->hit(root_, x, y);
        if (!kHit.has_value() || !kHit->node.has_value()) {
            return;
        }
        // The row is the node hit or the one its words are on.
        const ui::Node kNode = *kHit->node;
        if (kNode == undoNode_ || kNode == redoNode_) {
            endEdit();
            step(kNode == undoNode_ ? "authoring.undo" : "authoring.redo");
        } else if (kNode == newNode_ && !scene_.empty()) {
            endEdit();
            create();
        } else if (kNode == deleteNode_ && !entity_.empty()) {
            endEdit();
            Value operation = operationOn("scene.destroy_entity");
            commit(std::move(operation), "entity deleted", std::nullopt);
        } else if (const auto kRemove = std::ranges::find(removeButtons_, kNode, &RemoveButton::node);
                   kRemove != removeButtons_.end()) {
            endEdit();
            Value operation = operationOn("scene.remove_component");
            operation.add("component", Value::string(kRemove->component));
            commit(std::move(operation), kRemove->name + " removed", entity_);
        } else if (const auto kScene = std::ranges::find(sceneRows_, kNode); kScene != sceneRows_.end()) {
            showScene(static_cast<std::size_t>(kScene - sceneRows_.begin()));
        } else if (const auto kEntity = std::ranges::find(entityRows_, kNode); kEntity != entityRows_.end()) {
            showEntity(static_cast<std::size_t>(kEntity - entityRows_.begin()));
        } else if (const auto kFieldAt = std::ranges::find(fields_, kNode, &FieldRow::value);
                   kFieldAt != fields_.end()) {
            beginEdit(*kFieldAt);
        } else if (const auto kViewAt = std::ranges::find(viewFields_, kNode, &FieldRow::value);
                   kViewAt != viewFields_.end()) {
            beginEdit(*kViewAt);
        } else {
            endEdit();
        }
    }

    void showScene(std::size_t at) {
        endEdit();
        fields_.clear();
        const bool kChanged = scene_ != scenes_[at];
        if (kChanged) {
            // Each scene its own history.
            undoable_ = 0;
            redoable_ = 0;
            static_cast<void>(showHistory());
            if (previewing_) {
                static_cast<void>(ask(previewRecord(next(), scene_, nullptr)));
                previewing_ = false;
            }
        }
        scene_ = scenes_[at];
        sceneAt_ = at;
        if (kChanged) {
            // A scene's view is the session's, unknown here until it says.
            view_.reset();
            showView();
            if (preview_.has_value()) {
                const Answered kAttached = answeredOf(ask(previewRecord(next(), scene_, &*preview_)));
                previewing_ = kAttached.previewing;
                if (kAttached.view.has_value()) {
                    view_ = kAttached.view;
                }
                say(kAttached.done ? (previewing_ ? "previewing " + scene_ : "no preview") : kAttached.message);
                showViewText();
            }
        }
        for (std::size_t each = 0; each < sceneRows_.size(); ++each) {
            static_cast<void>(
                tree_->setLook(sceneRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
        }
        clear(entityRows_);
        clear(componentRows_);
        removeButtons_.clear();
        entities_.clear();
        names_.clear();
        brought_.clear();
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
            names_.push_back(name != nullptr && name->text() != nullptr ? *name->text() : std::string{});
            brought_.push_back(brought != nullptr);
        }
    }

    void showEntity(std::size_t at) {
        endEdit();
        fields_.clear();
        entityAt_ = at;
        entity_ = entities_[at];
        for (std::size_t each = 0; each < entityRows_.size(); ++each) {
            static_cast<void>(
                tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
        }
        clear(componentRows_);
        removeButtons_.clear();
        components_ = 0;
        // Chosen in the session, so undo and redo keep it (D417).
        static_cast<void>(ask(selectRecord(next(), scene_, entity_)));
        // The scene's own entity is named; an instance's is its source's.
        if (catalog_.offers("scene.rename_entity") && !brought_[at]) {
            auto line = fieldRow("name", names_[at]);
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::Name, .field = "name"});
        }
        const std::optional<Value> kRead = read(scene_, "scene.read_entity", entity_);
        const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
        const bool kListed = components != nullptr && components->kind() == Value::Kind::Array;
        for (const Value& each : kListed ? components->items() : std::span<const Value>{}) {
            const Value* name = each.find("name");
            const Value* component = each.find("component");
            const std::string kComponent =
                component != nullptr && component->text() != nullptr ? *component->text() : std::string{};
            if (!componentHeading(name != nullptr && name->text() != nullptr ? *name->text() : "?", kComponent)
                     .has_value()) {
                return;
            }
            ++components_;
            const Value* fields = each.find("fields");
            for (const Value& field : fields != nullptr ? fields->items() : std::span<const Value>{}) {
                const Value* fieldName = field.find("name");
                const Value* value = field.find("value");
                if (fieldName == nullptr || fieldName->text() == nullptr) {
                    continue;
                }
                auto line = fieldRow(*fieldName->text(), value != nullptr ? shown(*value) : std::string{});
                if (!line.has_value()) {
                    return;
                }
                componentRows_.push_back(line->first);
                const bool kTyped =
                    value != nullptr && value->kind() == Value::Kind::Object && value->names().size() == 1;
                fields_.push_back(FieldRow{.value = line->second,
                                           .component = kComponent,
                                           .field = *fieldName->text(),
                                           .kind = kTyped ? value->names().front() : std::string{}});
            }
        }
        if (catalog_.offers("scene.add_component")) {
            auto line = fieldRow("add", "");
            if (line.has_value()) {
                componentRows_.push_back(line->first);
                fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::Add, .field = "add"});
            }
        }
    }

    /// A component's heading row: its name, and Remove if offered.
    result::Status componentHeading(std::string_view name, const std::string& component) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kHeading,
                            box(componentsColumn_,
                                ui::Layout{.height = ui::pixels(28),
                                           .justify = ui::Justify::SpaceBetween,
                                           .alignItems = ui::Align::Center,
                                           .padding = {8, 0, 2, 0}},
                                kRow));
        componentRows_.push_back(kHeading);
        RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kHeading, ui::Layout{.padding = {0, 4, 0, 4}}, 0));
        RAWFRAME_TRY(words(kTitle, name, kText, 14));
        if (catalog_.offers("scene.remove_component")) {
            RAWFRAME_TRY_ASSIGN(const ui::Node kRemove,
                                box(kHeading, ui::Layout{.padding = {8, 2, 8, 2}, .margin = {0, 2, 0, 2}}, kPanel));
            RAWFRAME_TRY(words(kRemove, "Remove", kQuiet, 13));
            removeButtons_.push_back(RemoveButton{kRemove, component, std::string{name}});
        }
        return {};
    }

    /// A field's line: its name, and its value in a node that edits.
    result::Result<std::pair<ui::Node, ui::Node>>
    fieldRow(std::string_view name, std::string_view value, std::optional<ui::Node> column = std::nullopt) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kLine,
                            box(column.value_or(componentsColumn_),
                                ui::Layout{.height = ui::pixels(28),
                                           .direction = ui::Direction::Row,
                                           .alignItems = ui::Align::Center,
                                           .gap = 8,
                                           .padding = {8, 2, 8, 2}},
                                kPanel));
        RAWFRAME_TRY_ASSIGN(const ui::Node kName,
                            box(kLine, ui::Layout{.width = ui::pixels(120), .padding = {0, 2, 0, 2}}, 0));
        RAWFRAME_TRY(words(kName, name, kQuiet, 14));
        RAWFRAME_TRY_ASSIGN(const ui::Node kValue, tree_->addEditable(++keys_));
        RAWFRAME_TRY(tree_->setLayout(kValue, ui::Layout{.grow = 1, .padding = {6, 2, 6, 2}}));
        RAWFRAME_TRY(tree_->setLook(kValue, ui::Look{.fill = kField, .radius = 3}));
        RAWFRAME_TRY(tree_->attach(kLine, kValue));
        RAWFRAME_TRY(words(kValue, value, kText, 14));
        return std::pair{kLine, kValue};
    }

    /// `row`'s field takes the keyboard, its text chosen whole.
    void beginEdit(const FieldRow& row) {
        endEdit();
        editing_ = row;
        edit_ = std::make_unique<ui::TextEdit>(
            *tree_, row.value, ui::EditSettings{.caretColor = kText, .selectionColor = 0x4A6FA5AAU});
        static_cast<void>(edit_->press(ui::EditKey::SelectAll, {}));
        static_cast<void>(tree_->setLook(row.value, ui::Look{.fill = kEditing, .radius = 3}));
        if (typing_ != nullptr) {
            const auto kPlace = tree_->placeOf(root_, row.value);
            typing_->focus(kPlace.has_value()
                               ? view::UiTyping::Caret{kPlace->x, kPlace->y, kPlace->width, kPlace->height}
                               : view::UiTyping::Caret{});
        }
    }

    /// The keyboard let go.
    void endEdit() {
        if (edit_ == nullptr) {
            return;
        }
        edit_.reset();
        if (editing_.has_value() && tree_->contains(editing_->value)) {
            static_cast<void>(tree_->setLook(editing_->value, ui::Look{.fill = kField, .radius = 3}));
        }
        editing_.reset();
        if (typing_ != nullptr) {
            typing_->focus(std::nullopt);
        }
    }

    /// One thing typed into the field that holds the keyboard: Enter sets
    /// it, Escape puts back what the session holds.
    void take(const view::Typing& typing) {
        if (edit_ == nullptr || !editing_.has_value()) {
            return;
        }
        const std::optional<view::TypingKey> kLeft = view::edit(*edit_, typing);
        if (kLeft == view::TypingKey::Submit) {
            const FieldRow kEdited = *editing_;
            const std::string kTyped{tree_->textOf(kEdited.value)};
            endEdit();
            if (kEdited.role == FieldRow::Role::View) {
                setView(kEdited.field, kTyped);
            } else {
                apply(kEdited, kTyped);
            }
        } else if (kLeft == view::TypingKey::Dismiss) {
            const bool kView = editing_->role == FieldRow::Role::View;
            endEdit();
            if (kView) {
                showViewText();
            } else {
                showEntity(entityAt_);
            }
        }
    }

    /// The chosen scene's view section under its rows: the view's eye,
    /// target, and field of view, each a field.
    void showView() {
        clear(viewRows_);
        viewFields_.clear();
        auto heading = row(scenesColumn_, "View", kQuiet, kPanel);
        if (!heading.has_value()) {
            return;
        }
        viewRows_.push_back(*heading);
        for (const std::string_view kPart : {"eye", "target", "fieldOfView"}) {
            auto line = fieldRow(kPart == "fieldOfView" ? "field of view" : kPart, {}, scenesColumn_);
            if (!line.has_value()) {
                return;
            }
            viewRows_.push_back(line->first);
            viewFields_.push_back(
                FieldRow{.value = line->second, .role = FieldRow::Role::View, .field = std::string{kPart}});
        }
        showViewText();
    }

    /// The view's parts in their fields, as the session last said.
    void showViewText() {
        for (const FieldRow& kViewField : viewFields_) {
            static_cast<void>(words(kViewField.value, viewText(view_, kViewField.field), kText, 14));
        }
    }

    /// The scene's view with `part` as `text` says, set by the session
    /// (`authoring.view`), which hands it to a live preview.
    void setView(const std::string& part, const std::string& text) {
        const std::optional<Value> kView = viewWith(view_, part, text);
        if (!kView.has_value()) {
            ++refused_;
            say(part + ": " + (part == "fieldOfView" ? "one number" : "three numbers"));
            showViewText();
            return;
        }
        const Answered kAnswered = answeredOf(ask(viewRecord(next(), scene_, *kView)));
        if (kAnswered.done) {
            ++viewsSet_;
            view_ = kAnswered.view;
            previewing_ = kAnswered.previewing;
            say(previewing_ ? "view set and previewed" : "view set");
        } else {
            ++refused_;
            say(kAnswered.message);
        }
        showViewText();
    }

    /// `operation` with the chosen entity as its target.
    Value operationOn(std::string_view operation) const {
        Value made = Value::object();
        made.add("operation", Value::string(std::string{operation}));
        made.add("entity", Value::string(entity_));
        return made;
    }

    /// What `text`, given in `row`, asks of the session: the field set
    /// (`scene.set_field`), the entity renamed, or a component added; text
    /// that cannot be what the row asks is refused before it is asked.
    void apply(const FieldRow& row, const std::string& text) {
        Value operation;
        std::string done;
        if (row.role == FieldRow::Role::Name) {
            operation = operationOn("scene.rename_entity");
            operation.add("name", Value::string(text));
            done = "renamed " + text;
        } else if (row.role == FieldRow::Role::Add) {
            std::string why;
            const std::optional<Catalog::Component> kAdded = componentNamed(catalog_, text, why);
            if (!kAdded.has_value()) {
                refuse(why);
                return;
            }
            operation = operationOn("scene.add_component");
            operation.add("component", Value::string(kAdded->id));
            done = kAdded->name + " added";
        } else {
            const std::optional<Value> kValue = typedValue(row.kind, text);
            if (!kValue.has_value()) {
                refuse(row.field + ": " + (row.kind.empty() ? std::string{"not edited here"} : "not a " + row.kind));
                return;
            }
            operation = operationOn("scene.set_field");
            operation.add("component", Value::string(row.component));
            operation.add("field", Value::string(row.field));
            operation.add("value", *kValue);
            done = row.field + " set to " + text;
        }
        commit(std::move(operation), done, entity_);
    }

    /// A new entity at the end of the scene's own, its identity minted
    /// (D438), chosen once made.
    void create() {
        const std::string kIdentity = mintedIdentity();
        Value operation = Value::object();
        operation.add("operation", Value::string("scene.create_entity"));
        operation.add("entity", Value::string(kIdentity));
        operation.add("name", Value::string("new entity"));
        commit(std::move(operation), "entity created", kIdentity);
    }

    /// `why` in the header, nothing asked, the entity shown as it stands.
    void refuse(const std::string& why) {
        ++refused_;
        say(why);
        showEntity(entityAt_);
    }

    /// One operation asked of the session for the chosen scene; `done` or
    /// the session's message said; the scene shown again with `choose`
    /// chosen if it stands.
    void commit(Value operation, const std::string& done, const std::optional<std::string>& choose) {
        const Outcome kOutcome = outcomeOf(ask(applyRecord(next(), scene_, std::move(operation))));
        told(kOutcome);
        if (kOutcome.done) {
            ++applied_;
            say(done);
        } else {
            ++refused_;
            say(kOutcome.message);
        }
        refresh(choose);
    }

    /// The chosen scene listed again, `entity` chosen if it stands.
    void refresh(const std::optional<std::string>& entity) {
        showScene(sceneAt_);
        if (!entity.has_value()) {
            return;
        }
        if (const auto kAt = std::ranges::find(entities_, *entity); kAt != entities_.end()) {
            showEntity(static_cast<std::size_t>(kAt - entities_.begin()));
        }
    }

    /// The chosen scene's last change undone, or the last undone redone,
    /// then the scene shown again, its entity still chosen if it stands.
    void step(std::string_view kind) {
        if (scene_.empty()) {
            return;
        }
        const Outcome kOutcome = outcomeOf(ask(stepRecord(next(), kind, scene_)));
        told(kOutcome);
        const bool kUndo = kind == "authoring.undo";
        if (kOutcome.done) {
            ++(kUndo ? undone_ : redone_);
            say(kUndo ? "undone" : "redone");
        } else {
            say(kOutcome.message);
        }
        refresh(entity_.empty() ? std::nullopt : std::optional{entity_});
    }

    /// What can be undone and redone, as an outcome says.
    void told(const Outcome& outcome) {
        undoable_ = outcome.undoable;
        redoable_ = outcome.redoable;
        static_cast<void>(showHistory());
        if (outcome.view.has_value()) {
            view_ = outcome.view;
            showViewText();
        }
    }

    /// Undo and redo, quiet when there is nothing to do.
    result::Status showHistory() {
        RAWFRAME_TRY(words(undoNode_, "Undo", undoable_ > 0 ? kText : kQuiet, 14));
        return words(redoNode_, "Redo", redoable_ > 0 ? kText : kQuiet, 14);
    }

    /// `text` in the header's status.
    void say(std::string text) {
        status_ = std::move(text);
        static_cast<void>(words(statusNode_, status_, kQuiet, 14));
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
    Catalog catalog_;
    std::vector<std::string> names_;
    std::vector<bool> brought_;
    ui::Node newNode_{};
    ui::Node deleteNode_{};
    std::vector<RemoveButton> removeButtons_;
    view::UiPointing* pointing_ = nullptr;
    view::UiTyping* typing_ = nullptr;
    std::vector<view::Typing> typed_;
    std::unique_ptr<ui::TextEdit> edit_;
    std::vector<FieldRow> fields_;
    std::optional<FieldRow> editing_;
    std::optional<Preview> preview_;
    bool previewing_ = false;
    std::optional<Value> view_;
    std::vector<ui::Node> viewRows_;
    std::vector<FieldRow> viewFields_;
    std::uint64_t viewsSet_ = 0;
    std::size_t entityAt_ = 0;
    ui::Node statusNode_{};
    ui::Node undoNode_{};
    ui::Node redoNode_{};
    std::size_t sceneAt_ = 0;
    std::int64_t undoable_ = 0;
    std::int64_t redoable_ = 0;
    std::uint64_t undone_ = 0;
    std::uint64_t redone_ = 0;
    std::string status_;
    std::uint64_t applied_ = 0;
    std::uint64_t refused_ = 0;
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
