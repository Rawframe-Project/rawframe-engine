#include "generated/studio_font.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/document/json.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/studio/registrar.h"
#include "rawframe/ui/frames.h"
#include "rawframe/ui/text_edit.h"
#include "rawframe/ui/tree.h"
#include "rawframe/view/pointing.h"
#include "rawframe/view/typing.h"

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

/// What `text` typed into a field of `kind` (a read's value key) asks the
/// session to set it to; none for a kind Studio does not edit, or text
/// that is not one of its values. The session checks it again.
std::optional<Value> typedValue(std::string_view kind, std::string_view text) {
    if (kind == "real") {
        const auto kParsed = document::parse(text);
        if (!kParsed.has_value() || kParsed->kind() != Value::Kind::Number) {
            return std::nullopt;
        }
        Value made = Value::object();
        made.add("real", *kParsed);
        return made;
    }
    if (kind == "signed" || kind == "unsigned" || kind == "case") {
        Value made = Value::object();
        made.add(std::string{kind}, Value::string(std::string{text}));
        return made;
    }
    if (kind == "truth" && (text == "true" || text == "false")) {
        Value made = Value::object();
        made.add("truth", Value::boolean(text == "true"));
        return made;
    }
    return std::nullopt;
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

/// A field's value node and what the session needs to set it.
struct FieldRow {
    ui::Node value{};
    std::string component;
    std::string field;
    std::string kind;
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
        RAWFRAME_TRY(tree_->setLayout(
            kHeader,
            ui::Layout{.height = ui::pixels(34), .justify = ui::Justify::SpaceBetween, .padding = {12, 6, 12, 6}}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kHeader, ui::Layout{}, 0));
        RAWFRAME_TRY(words(kTitle, title_, kText));
        RAWFRAME_TRY_ASSIGN(statusNode_, box(kHeader, ui::Layout{}, 0));
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
        } else if (const auto kFieldAt = std::ranges::find(fields_, kNode, &FieldRow::value);
                   kFieldAt != fields_.end()) {
            beginEdit(static_cast<std::size_t>(kFieldAt - fields_.begin()));
        } else {
            endEdit();
        }
    }

    void showScene(std::size_t at) {
        endEdit();
        fields_.clear();
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
        endEdit();
        fields_.clear();
        entityAt_ = at;
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
            const Value* component = each.find("component");
            const std::string kComponent =
                component != nullptr && component->text() != nullptr ? *component->text() : std::string{};
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
    }

    /// A field's line: its name, and its value in a node that edits.
    result::Result<std::pair<ui::Node, ui::Node>> fieldRow(std::string_view name, std::string_view value) {
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kLine,
            box(componentsColumn_,
                ui::Layout{
                    .height = ui::pixels(28), .direction = ui::Direction::Row, .gap = 8, .padding = {8, 2, 8, 2}},
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

    /// The field at `at` takes the keyboard, its text chosen whole.
    void beginEdit(std::size_t at) {
        endEdit();
        editing_ = at;
        edit_ = std::make_unique<ui::TextEdit>(
            *tree_, fields_[at].value, ui::EditSettings{.caretColor = kText, .selectionColor = 0x4A6FA5AAU});
        static_cast<void>(edit_->press(ui::EditKey::SelectAll, {}));
        static_cast<void>(tree_->setLook(fields_[at].value, ui::Look{.fill = kEditing, .radius = 3}));
        if (typing_ != nullptr) {
            const auto kPlace = tree_->placeOf(root_, fields_[at].value);
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
        if (editing_ < fields_.size()) {
            static_cast<void>(tree_->setLook(fields_[editing_].value, ui::Look{.fill = kField, .radius = 3}));
        }
        if (typing_ != nullptr) {
            typing_->focus(std::nullopt);
        }
    }

    /// One thing typed into the field that holds the keyboard: Enter sets
    /// it, Escape puts back what the session holds.
    void take(const view::Typing& typing) {
        if (edit_ == nullptr) {
            return;
        }
        const std::optional<view::TypingKey> kLeft = view::edit(*edit_, typing);
        if (kLeft == view::TypingKey::Submit) {
            const FieldRow kEdited = fields_[editing_];
            const std::string kTyped{tree_->textOf(kEdited.value)};
            endEdit();
            apply(kEdited, kTyped);
        } else if (kLeft == view::TypingKey::Dismiss) {
            endEdit();
            showEntity(entityAt_);
        }
    }

    /// `text` set into the field by the session (`scene.set_field` in an
    /// atomic `authoring.apply`), then the entity read again; what came of
    /// it said in the header.
    void apply(const FieldRow& row, const std::string& text) {
        const std::optional<Value> kValue = typedValue(row.kind, text);
        if (!kValue.has_value()) {
            ++refused_;
            say(row.field + ": " + (row.kind.empty() ? std::string{"not edited here"} : "not a " + row.kind));
            showEntity(entityAt_);
            return;
        }
        Value operation = Value::object();
        operation.add("operation", Value::string("scene.set_field"));
        operation.add("entity", Value::string(entity_));
        operation.add("component", Value::string(row.component));
        operation.add("field", Value::string(row.field));
        operation.add("value", *kValue);
        Value operations = Value::array();
        operations.push(std::move(operation));
        Value request = Value::object();
        request.add("formatVersion", Value::integer(1));
        request.add("kind", Value::string("authoring.request"));
        request.add("batch", Value::string("atomic"));
        request.add("operations", std::move(operations));
        Value record = Value::object();
        record.add("kind", Value::string("authoring.apply"));
        record.add("id", Value::integer(static_cast<std::int64_t>(records_ + 1)));
        record.add("scene", Value::string(scene_));
        record.add("request", std::move(request));
        const auto kParsed = document::parse(ask(record));
        // Applied when the one slot holds deltas.
        const Value* answer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
        const Value* results = answer != nullptr ? answer->find("results") : nullptr;
        const bool kSlot = results != nullptr && results->kind() == Value::Kind::Array && !results->items().empty();
        const Value* slot = kSlot ? &results->items()[0] : nullptr;
        if (slot != nullptr && slot->find("deltas") != nullptr) {
            ++applied_;
            say(row.field + " set to " + text);
        } else {
            ++refused_;
            const Value* error =
                slot != nullptr ? slot->find("error") : (kParsed.has_value() ? kParsed->find("error") : nullptr);
            const Value* message = error != nullptr ? error->find("message") : nullptr;
            say(row.field + ": " + (message != nullptr && message->text() != nullptr ? *message->text() : "refused"));
        }
        showEntity(entityAt_);
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
    view::UiPointing* pointing_ = nullptr;
    view::UiTyping* typing_ = nullptr;
    std::vector<view::Typing> typed_;
    std::unique_ptr<ui::TextEdit> edit_;
    std::vector<FieldRow> fields_;
    std::size_t editing_ = 0;
    std::size_t entityAt_ = 0;
    ui::Node statusNode_{};
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
