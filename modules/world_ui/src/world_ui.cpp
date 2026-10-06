#include "rawframe/world_ui/world_ui.h"

#include "rawframe/ui/text_edit.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_ui/errors.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace rawframe::world_ui {

namespace {

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldUiDomain, code(WorldUiError::BadNodes), why).error()};
}

/// A size along one axis: automatic at nought and nought.
ui::Dimension dimensionOf(float scale, float offset) noexcept {
    return scale == 0 && offset == 0 ? ui::Dimension{}
                                     : ui::Dimension{.automatic = false, .scale = scale, .offset = offset};
}

/// `node`'s layout as the tree takes it; none for a number past its
/// constants.
std::optional<ui::Layout> layoutOf(const Node& node) noexcept {
    if (node.direction > 3 || node.justify > 5 || node.alignItems > 4 || node.alignSelf > 4) {
        return std::nullopt;
    }
    const auto kSides = [](float value) {
        return std::array<float, 4>{value, value, value, value};
    };
    return ui::Layout{
        .width = dimensionOf(node.widthScale, node.widthOffset),
        .height = dimensionOf(node.heightScale, node.heightOffset),
        .direction = static_cast<ui::Direction>(node.direction),
        .justify = static_cast<ui::Justify>(node.justify),
        // A parent's AUTO stretches.
        .alignItems = node.alignItems == 0 ? ui::Align::Stretch : static_cast<ui::Align>(node.alignItems),
        .gap = node.gap,
        .grow = node.grow,
        .shrink = node.shrink,
        .alignSelf = static_cast<ui::Align>(node.alignSelf),
        .padding = kSides(node.padding),
        .margin = kSides(node.margin),
        .border = kSides(node.border),
        .placement = {.absolute = node.absolute != 0,
                      .x = {.automatic = false, .scale = node.xScale, .offset = node.xOffset},
                      .y = {.automatic = false, .scale = node.yScale, .offset = node.yOffset},
                      .anchorX = node.anchorX,
                      .anchorY = node.anchorY},
    };
}

ui::Look lookOf(const Node& node) noexcept {
    const float kSlice = node.imageSlice;
    // A kind past the set is passed on, for the tree to refuse.
    const ui::GradientLook kGradient = node.gradientKind == 0
                                           ? ui::GradientLook{}
                                           : ui::GradientLook{.kind = static_cast<ui::GradientLook::Kind>(
                                                                  std::min<std::uint32_t>(node.gradientKind, 255)),
                                                              .angle = node.gradientAngle,
                                                              .colors = {node.gradientFrom, node.gradientTo},
                                                              .positions = {0, 1},
                                                              .stops = 2};
    return ui::Look{
        .fill = node.fill,
        .borderColor = node.borderColor,
        .radius = node.radius,
        .clip = node.clip != 0,
        .image = node.image,
        .imageSlice = {kSlice, kSlice, kSlice, kSlice},
        // Nought draws it as it is.
        .imageTint = node.imageTint != 0 ? node.imageTint : 0xFFFFFFFF,
        .outerShadow = {.color = node.shadowColor, .x = node.shadowX, .y = node.shadowY, .blur = node.shadowBlur},
        .gradient = kGradient};
}

/// `node`'s part in what presses hit (D421); none for a number past its
/// constants. Nought passes presses through to its children and past them.
std::optional<ui::Interaction> interactionOf(const Node& node) noexcept {
    if (node.hit > 2 || node.layer > 3 || node.edit > 3) {
        return std::nullopt;
    }
    constexpr std::array<ui::Interaction::Hits, 3> kHits = {
        ui::Interaction::Hits::Children, ui::Interaction::Hits::Itself, ui::Interaction::Hits::Nothing};
    // A text field takes the presses that land on it (D426), unless it is
    // left out with its children.
    const bool kField = node.edit != 0 && node.hit != 2;
    return ui::Interaction{.hits = kField ? ui::Interaction::Hits::Itself : kHits[node.hit],
                           .passThrough = node.hit == 0 && !kField,
                           .layer = static_cast<ui::Interaction::Layer>(node.layer)};
}

/// `node`'s text look.
ui::TextLook textLookOf(const Node& node, ui::Font font) noexcept {
    return ui::TextLook{.font = font,
                        .size = node.textSize > 0 ? node.textSize : 16,
                        .color = node.textColor,
                        .align = static_cast<ui::TextAlign>(node.textAlign),
                        .wrap = node.textWrap == 0};
}

/// How a field's text is edited: its kind and room, and its caret and
/// selection in its text's color.
ui::EditSettings editSettingsOf(const Node& node) noexcept {
    const std::uint32_t kColor = node.textColor != 0 ? node.textColor : 0x000000FF;
    return ui::EditSettings{.multiline = node.edit == 3,
                            .maximumBytes =
                                node.editLimit == 0 ? kMostTypedBytes : std::min(node.editLimit, kMostTypedBytes),
                            .caretColor = kColor,
                            .selectionColor = (kColor & 0xFFFFFF00U) | 0x55U};
}

ui::EditKey editKeyOf(view::TypingKey key) noexcept {
    switch (key) {
    case view::TypingKey::Backspace:
        return ui::EditKey::Backspace;
    case view::TypingKey::Delete:
        return ui::EditKey::Delete;
    case view::TypingKey::Left:
        return ui::EditKey::Left;
    case view::TypingKey::Right:
        return ui::EditKey::Right;
    case view::TypingKey::Up:
        return ui::EditKey::Up;
    case view::TypingKey::Down:
        return ui::EditKey::Down;
    case view::TypingKey::Home:
        return ui::EditKey::Home;
    case view::TypingKey::End:
        return ui::EditKey::End;
    default:
        return ui::EditKey::SelectAll;
    }
}

/// The window's root and each view's: what they do not cover passes
/// through.
constexpr ui::Interaction kThrough{.hits = ui::Interaction::Hits::Children, .passThrough = true};

std::uint64_t keyOf(ui::Node node) noexcept {
    return (std::uint64_t{node.index1} << 32U) | node.generation;
}

std::uint64_t keyOf(world::EntityHandle entity) noexcept {
    return (std::uint64_t{entity.slot} << 32U) | entity.generation;
}

/// A node component of an entity, and the node it is in the tree: none
/// while its value is refused or there was no room for it.
struct Entry {
    std::optional<ui::Node> node;
    Node value;
    /// The words it shows, as last read (D427).
    Typed words;
    bool seen = false;
    /// Its node was made a text field (D426), and shows its label as the
    /// field's placeholder now, not what was typed (D428).
    bool editable = false;
    bool placeholder = false;
};

/// The text field holding the keyboard, and its editing.
struct Focus {
    Entry* entry = nullptr;
    std::unique_ptr<ui::TextEdit> edit;
};

using EntryKey = std::pair<world::EntityHandle, std::uint32_t>;

/// A view's root, sized to its rectangle, and the nodes its World holds.
struct ViewState {
    world::World* world = nullptr;
    std::optional<ui::Node> root;
    std::array<float, 4> placed{-1, -1, -1, -1};
    std::vector<world::ColumnQuery> queries;
    /// Each node component's words component in the World, if it shows one.
    std::vector<std::optional<schema::ComponentRuntimeId>> shows;
    std::map<EntryKey, Entry> entries;
};

/// The words `typed` holds; none when it holds none or says more than it
/// can hold.
std::optional<std::string_view> wordsOf(const Typed& typed) noexcept {
    if (typed.length == 0 || typed.length > kMostTypedBytes) {
        return std::nullopt;
    }
    return std::string_view{reinterpret_cast<const char*>(typed.bytes.data()), typed.length};
}

/// A child in its parent's order: by `order`, then entity, then component.
struct Placed {
    std::int32_t order = 0;
    world::EntityHandle entity;
    std::uint32_t component = 0;
    ui::Node node;
};

} // namespace

struct WorldUi::State {
    UiSettings settings;
    std::unique_ptr<ui::Tree> tree;
    /// The fonts read, by their game identities.
    std::unordered_map<std::uint64_t, ui::Font> fonts;
    ui::Node window;
    std::vector<ViewState> views;
    /// Each parent's children as last attached, by the parent's node.
    std::unordered_map<std::uint64_t, std::vector<ui::Node>> attached;
    std::uint32_t held = 0;
    ui::DrawList drawn;
    UiStatistics statistics;
    std::optional<Focus> focus;
    std::optional<view::UiTyping::Caret> caret;
    std::vector<view::Submitted> submitted;

    /// The keyboard taken from the field holding it, which shows its
    /// placeholder again if it is empty; `shown` false for a field going.
    void letGo(bool shown = true) {
        Entry* holding = focus.has_value() ? focus->entry : nullptr;
        focus.reset();
        caret.reset();
        if (holding != nullptr && shown) {
            static_cast<void>(giveFieldLook(*holding));
        }
    }

    /// Where the caret of the field holding the keyboard is, in the window,
    /// as the tree was last laid out; none while none does.
    void placeCaret() {
        caret.reset();
        if (!focus.has_value()) {
            return;
        }
        const auto kPlace = tree->placeOf(window, focus->edit->node());
        const auto kCaret = focus->edit->caretRect();
        if (kPlace.has_value() && kCaret.has_value()) {
            caret = view::UiTyping::Caret{kPlace->x + kCaret->x, kPlace->y + kCaret->y, kCaret->width, kCaret->height};
        }
    }

    /// `entry`'s node gone, its children first made roots so they live on.
    void drop(Entry& entry) {
        if (!entry.node.has_value()) {
            return;
        }
        if (focus.has_value() && focus->entry == &entry) {
            letGo(false);
        }
        if (const auto kChildren = attached.find(keyOf(*entry.node)); kChildren != attached.end()) {
            for (const ui::Node kChild : kChildren->second) {
                if (tree->contains(kChild)) {
                    static_cast<void>(tree->detach(kChild));
                }
            }
            attached.erase(kChildren);
        }
        static_cast<void>(tree->remove(*entry.node));
        entry.node.reset();
        --held;
    }

    /// `value` given to `entry`, its node made if it has none; whether the
    /// tree took it.
    bool give(Entry& entry, world::EntityHandle entity, const Node& value) {
        entry.value = value;
        const std::optional<ui::Layout> kLayout = layoutOf(value);
        const std::optional<ui::Interaction> kInteraction = interactionOf(value);
        // A node that becomes a text field, or stops being one, is made
        // again: a field's text is found by a key its node is made with.
        const bool kEditable = value.edit != 0;
        if (entry.node.has_value() && entry.editable != kEditable) {
            drop(entry);
        }
        if (!entry.node.has_value()) {
            if (!kLayout.has_value() || !kInteraction.has_value() || held >= settings.maximumNodes) {
                return false;
            }
            auto made = kEditable ? tree->addEditable(keyOf(entity)) : tree->add(keyOf(entity));
            entry.editable = kEditable;
            if (!made.has_value()) {
                return false;
            }
            entry.node = *made;
            ++held;
            ++statistics.made;
        } else {
            ++statistics.changed;
        }
        if (!kLayout.has_value() || !kInteraction.has_value() || !tree->setLayout(*entry.node, *kLayout).has_value() ||
            !tree->setLook(*entry.node, lookOf(value)).has_value() ||
            !tree->setInteraction(*entry.node, *kInteraction).has_value() ||
            !(kEditable ? giveFieldLook(entry) : giveWords(*entry.node, value, wordsOf(entry.words)))) {
            drop(entry);
            return false;
        }
        // A field changed while it holds the keyboard is edited as it says
        // now.
        if (focus.has_value() && focus->entry == &entry) {
            focus->edit->configure(editSettingsOf(value));
        }
        return true;
    }

    /// A text field shows what was typed into it in its text look, never a
    /// label (D426); empty and without the keyboard, its label's words as
    /// its placeholder, at half its text's alpha (D428). Whether the tree
    /// took it.
    bool giveFieldLook(Entry& entry) {
        const Node& kValue = entry.value;
        if (kValue.textAlign > 2 || kValue.textWrap > 1) {
            return false;
        }
        const auto kFont = fonts.find(kValue.font);
        ui::TextLook look = textLookOf(kValue, kFont != fonts.end() ? kFont->second : ui::Font{});
        std::string text = entry.placeholder ? std::string{} : std::string{tree->textOf(*entry.node)};
        const bool kHeld = focus.has_value() && focus->entry == &entry;
        std::optional<std::string> label;
        if (!kHeld && text.empty() && kValue.text != 0 && settings.words) {
            label = settings.words(kValue.text, kValue.textValue);
        }
        entry.placeholder = label.has_value();
        if (label.has_value()) {
            text = std::move(*label);
            const std::uint32_t kAlpha = look.color & 0xFFU;
            look.color = (look.color & 0xFFFFFF00U) | (kAlpha / 2);
        }
        return tree->setText(*entry.node, text, look).has_value();
    }

    /// `value`'s words given to `node`, or none, the typed `shown` in place
    /// of its label (D427); whether the tree took them.
    bool giveWords(ui::Node node, const Node& value, std::optional<std::string_view> shown = std::nullopt) {
        if (value.textAlign > 2 || value.textWrap > 1) {
            return false;
        }
        std::optional<std::string> words;
        if (shown.has_value()) {
            words = std::string{*shown};
            ++statistics.typedShown;
        } else if (value.text != 0) {
            words = settings.words ? settings.words(value.text, value.textValue) : std::nullopt;
            ++(words.has_value() ? statistics.texts : statistics.textsUnknown);
        }
        if (!words.has_value()) {
            return tree->clearText(node).has_value();
        }
        // A font not read yet is shown in the default until it is.
        const auto kFont = fonts.find(value.font);
        return tree
            ->setText(node,
                      *words,
                      {.font = kFont != fonts.end() ? kFont->second : ui::Font{},
                       .size = value.textSize > 0 ? value.textSize : 16,
                       .color = value.textColor,
                       .align = static_cast<ui::TextAlign>(value.textAlign),
                       .wrap = value.textWrap == 0})
            .has_value();
    }

    /// `view` bound to `world`: every node of the World before gone.
    result::Status bind(ViewState& view, world::World* world) {
        for (auto& [kKey, entry] : view.entries) {
            drop(entry);
        }
        view.entries.clear();
        view.queries.clear();
        view.world = world;
        if (world == nullptr) {
            return {};
        }
        for (const schema::ComponentTypeId kId : settings.nodes) {
            const auto kComponent = world->registry().find(kId);
            if (!kComponent.has_value()) {
                return refuse("a UI node component is not in the World");
            }
            if (world->registry().descriptor(*kComponent).size != sizeof(Node)) {
                return refuse("a UI node component is not rawframe.ui's size");
            }
            const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kComponent, world::Access::Read}};
            RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kTerms, world->registry()));
            view.queries.push_back(std::move(query));
        }
        view.shows.assign(settings.nodes.size(), std::nullopt);
        for (std::size_t at = 0; at < settings.shows.size() && at < settings.nodes.size(); ++at) {
            if (!settings.shows[at].has_value()) {
                continue;
            }
            const auto kWords = world->registry().find(*settings.shows[at]);
            if (!kWords.has_value() || world->registry().descriptor(*kWords).size != sizeof(Typed)) {
                return refuse("a UI node's words are not a rawframe.ui.Typed component in the World");
            }
            view.shows[at] = *kWords;
        }
        return {};
    }

    /// `view`'s nodes made, changed, and removed as its World's components
    /// are.
    void mirror(ViewState& view) {
        for (auto& [kKey, entry] : view.entries) {
            entry.seen = false;
        }
        for (std::uint32_t component = 0; component < view.queries.size(); ++component) {
            view.queries[component].forEachChunk(*view.world, [&](const world::ColumnChunk& chunk) {
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    Node value;
                    std::memcpy(&value, chunk.columns[0] + (row * sizeof(Node)), sizeof(Node));
                    auto [at, made] = view.entries.try_emplace(EntryKey{chunk.entities[row], component});
                    Entry& entry = at->second;
                    entry.seen = true;
                    // Its words, read where it shows any (D427).
                    Typed words;
                    if (view.shows[component].has_value()) {
                        if (const void* kHeld = view.world->getErased(chunk.entities[row], *view.shows[component])) {
                            std::memcpy(&words, kHeld, sizeof(Typed));
                        }
                    }
                    const bool kWordsChanged = std::memcmp(&entry.words, &words, sizeof(Typed)) != 0;
                    entry.words = words;
                    // Unchanged, a node costs nothing; one refused is tried
                    // again only once it changes.
                    if (!made && !kWordsChanged && std::memcmp(&entry.value, &value, sizeof(Node)) == 0) {
                        if (!entry.node.has_value()) {
                            ++statistics.leftOut;
                        }
                        continue;
                    }
                    if (!give(entry, chunk.entities[row], value)) {
                        ++statistics.leftOut;
                    }
                }
            });
        }
        for (auto at = view.entries.begin(); at != view.entries.end();) {
            if (at->second.seen) {
                ++at;
                continue;
            }
            drop(at->second);
            at = view.entries.erase(at);
        }
    }

    /// Every node of `view` in its parent, in order: its component's
    /// parent on its entity or the player, or the view's root.
    void
    nest(ViewState& view, world::EntityHandle player, std::unordered_map<std::uint64_t, std::vector<Placed>>& into) {
        const auto kNodeOf = [&view](world::EntityHandle entity, std::size_t component) -> std::optional<ui::Node> {
            const auto kFound = view.entries.find(EntryKey{entity, static_cast<std::uint32_t>(component)});
            return kFound != view.entries.end() ? kFound->second.node : std::nullopt;
        };
        for (const auto& [kKey, kEntry] : view.entries) {
            if (!kEntry.node.has_value()) {
                continue;
            }
            const auto& [kEntity, kComponent] = kKey;
            std::optional<ui::Node> parent = view.root;
            if (const std::optional<std::size_t> kParent = settings.parents[kComponent]) {
                parent = kNodeOf(kEntity, *kParent);
                if (!parent.has_value() && !player.isNull()) {
                    parent = kNodeOf(player, *kParent);
                }
            }
            if (!parent.has_value()) {
                ++statistics.leftOut;
                continue;
            }
            into[keyOf(*parent)].push_back(
                Placed{.order = kEntry.value.order, .entity = kEntity, .component = kComponent, .node = *kEntry.node});
        }
    }

    /// Each parent's children as `wanted` has them, moved only where they
    /// differ from what is attached.
    result::Status attach(std::unordered_map<std::uint64_t, std::vector<Placed>>& wanted) {
        std::vector<std::pair<ui::Node, std::vector<ui::Node>>> moved;
        const auto kParentOf = [](std::uint64_t key) {
            return ui::Node{.index1 = static_cast<std::uint32_t>(key >> 32U),
                            .generation = static_cast<std::uint32_t>(key)};
        };
        for (auto& [kParent, children] : wanted) {
            std::ranges::sort(children, [](const Placed& left, const Placed& right) {
                return std::tie(left.order, left.entity, left.component) <
                       std::tie(right.order, right.entity, right.component);
            });
            std::vector<ui::Node> nodes;
            nodes.reserve(children.size());
            for (const Placed& kChild : children) {
                nodes.push_back(kChild.node);
            }
            const auto kAttached = attached.find(kParent);
            if (kAttached == attached.end() || kAttached->second != nodes) {
                moved.emplace_back(kParentOf(kParent), std::move(nodes));
            }
        }
        for (const auto& [kParent, kChildren] : attached) {
            if (!wanted.contains(kParent) && !kChildren.empty()) {
                moved.emplace_back(kParentOf(kParent), std::vector<ui::Node>{});
            }
        }
        // Every child that moves let go of first, then each placed anew,
        // so none is ever under two parents.
        for (const auto& [kParent, kChildren] : moved) {
            if (const auto kAttached = attached.find(keyOf(kParent)); kAttached != attached.end()) {
                for (const ui::Node kChild : kAttached->second) {
                    if (tree->contains(kChild)) {
                        RAWFRAME_TRY(tree->detach(kChild));
                    }
                }
            }
            for (const ui::Node kChild : kChildren) {
                RAWFRAME_TRY(tree->detach(kChild));
            }
        }
        for (auto& [kParent, children] : moved) {
            for (const ui::Node kChild : children) {
                RAWFRAME_TRY(tree->attach(kParent, kChild));
            }
            if (children.empty()) {
                attached.erase(keyOf(kParent));
            } else {
                attached[keyOf(kParent)] = std::move(children);
            }
        }
        return {};
    }
};

WorldUi::WorldUi(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

WorldUi::~WorldUi() = default;

result::Result<std::unique_ptr<WorldUi>> WorldUi::create(UiSettings settings) {
    if (settings.parents.size() != settings.nodes.size() ||
        std::ranges::any_of(settings.parents, [&settings](const std::optional<std::size_t>& parent) {
            return parent.has_value() && *parent >= settings.nodes.size();
        })) {
        return refuse("each UI node component's parent is another of them");
    }
    auto state = std::make_unique<State>();
    // Room for the window's root and a root for each view beside the
    // game's nodes.
    RAWFRAME_TRY_ASSIGN(state->tree, ui::Tree::create(settings.maximumNodes + 64));
    RAWFRAME_TRY_ASSIGN(state->window, state->tree->add(0));
    RAWFRAME_TRY(state->tree->setLayout(state->window, {.width = ui::share(1), .height = ui::share(1)}));
    RAWFRAME_TRY(state->tree->setInteraction(state->window, kThrough));
    state->settings = std::move(settings);
    return std::unique_ptr<WorldUi>{new WorldUi{std::move(state)}};
}

result::Status WorldUi::update(std::span<const UiView> views, float width, float height, float scale) {
    State& state = *state_;
    // A view's root placed in the window over its rectangle, its roots in a
    // column from its top left, each its own size.
    while (state.views.size() > views.size()) {
        ViewState& last = state.views.back();
        RAWFRAME_TRY(state.bind(last, nullptr));
        if (last.root.has_value()) {
            state.attached.erase(keyOf(*last.root));
            RAWFRAME_TRY(state.tree->remove(*last.root));
        }
        state.views.pop_back();
    }
    state.views.resize(views.size());
    std::unordered_map<std::uint64_t, std::vector<Placed>> wanted;
    for (std::size_t at = 0; at < views.size(); ++at) {
        const UiView& kView = views[at];
        ViewState& view = state.views[at];
        if (!view.root.has_value()) {
            RAWFRAME_TRY_ASSIGN(view.root, state.tree->add(0));
            RAWFRAME_TRY(state.tree->attach(state.window, *view.root));
            RAWFRAME_TRY(state.tree->setInteraction(*view.root, kThrough));
        }
        if (const std::array<float, 4> kPlaced{kView.x, kView.y, kView.width, kView.height}; view.placed != kPlaced) {
            RAWFRAME_TRY(state.tree->setLayout(
                *view.root,
                {.width = ui::pixels(kView.width),
                 .height = ui::pixels(kView.height),
                 .direction = ui::Direction::Column,
                 .alignItems = ui::Align::Start,
                 .placement = {.absolute = true, .x = ui::pixels(kView.x), .y = ui::pixels(kView.y)}}));
            view.placed = kPlaced;
        }
        if (view.world != kView.world) {
            RAWFRAME_TRY(state.bind(view, kView.world));
        }
        if (view.world == nullptr) {
            continue;
        }
        state.mirror(view);
        state.nest(view, kView.player, wanted);
    }
    RAWFRAME_TRY(state.attach(wanted));
    RAWFRAME_TRY(state.tree->layOut(state.window, width, height));
    RAWFRAME_TRY(state.tree->draw(state.window, scale, state.drawn));
    // The field holding the keyboard: its caret and selection drawn over
    // the tree, and where the caret is told for the input method (D426).
    if (state.focus.has_value()) {
        static_cast<void>(state.focus->edit->decorate(state.window, state.drawn));
    }
    state.placeCaret();
    ++state.statistics.frames;
    state.statistics.mostNodes = std::max<std::uint64_t>(state.statistics.mostNodes, state.held);
    return {};
}

result::Status WorldUi::addFont(std::uint64_t id, std::span<const std::byte> bytes) {
    State& state = *state_;
    const auto kDeclared = std::ranges::find(state.settings.fonts, id);
    if (kDeclared == state.settings.fonts.end() || state.fonts.contains(id)) {
        return refuse("a font is one of the game's, read once");
    }
    RAWFRAME_TRY_ASSIGN(const ui::Font kFont, state.tree->addFont(bytes));
    state.fonts.emplace(id, kFont);
    // The default is the earliest declared font read so far; nought names
    // it too.
    for (const std::uint64_t kId : state.settings.fonts) {
        if (const auto kRead = state.fonts.find(kId); kRead != state.fonts.end()) {
            RAWFRAME_TRY(state.tree->setDefaultFont(kRead->second));
            break;
        }
    }
    for (ViewState& view : state.views) {
        for (auto& [kKey, entry] : view.entries) {
            const bool kShows = entry.editable || entry.value.text != 0 || wordsOf(entry.words).has_value();
            if (entry.node.has_value() && kShows &&
                (entry.value.font == id || !state.fonts.contains(entry.value.font))) {
                // A field keeps what was typed into it.
                const bool kGiven = entry.editable ? state.giveFieldLook(entry)
                                                   : state.giveWords(*entry.node, entry.value, wordsOf(entry.words));
                if (!kGiven) {
                    state.drop(entry);
                }
            }
        }
    }
    return {};
}

std::optional<std::int64_t> WorldUi::press(float x, float y) const {
    const State& state = *state_;
    const auto kHit = state.tree->hit(state.window, x, y);
    if (!kHit.has_value() || !kHit->node.has_value() || kHit->passThrough) {
        return std::nullopt;
    }
    for (const ViewState& kView : state.views) {
        for (const auto& [kKey, kEntry] : kView.entries) {
            if (kEntry.node == kHit->node) {
                return kEntry.value.press;
            }
        }
    }
    // Blocked, by a node that says nothing.
    return std::int64_t{0};
}

void WorldUi::pressAt(float x, float y) {
    State& state = *state_;
    const auto kHit = state.tree->hit(state.window, x, y);
    if (kHit.has_value() && kHit->node.has_value()) {
        for (ViewState& view : state.views) {
            for (auto& [kKey, entry] : view.entries) {
                if (entry.node != kHit->node || !entry.editable) {
                    continue;
                }
                if (!state.focus.has_value() || state.focus->entry != &entry) {
                    // Another field lets go first; this one's placeholder
                    // goes before its caret is made.
                    state.letGo();
                    state.focus = Focus{.entry = &entry, .edit = nullptr};
                    static_cast<void>(state.giveFieldLook(entry));
                    state.focus->edit =
                        std::make_unique<ui::TextEdit>(*state.tree, *entry.node, editSettingsOf(entry.value));
                    ++state.statistics.focused;
                }
                static_cast<void>(state.focus->edit->pointAt(kHit->x, kHit->y, false));
                state.placeCaret();
                return;
            }
        }
    }
    state.letGo();
}

void WorldUi::type(const view::Typing& typing) {
    State& state = *state_;
    if (!state.focus.has_value()) {
        return;
    }
    ui::TextEdit& edit = *state.focus->edit;
    const Node& kField = state.focus->entry->value;
    ++state.statistics.typed;
    switch (typing.kind) {
    case view::Typing::Kind::Text:
        static_cast<void>(edit.type(typing.text));
        return;
    case view::Typing::Kind::Composition: {
        std::vector<ui::CompositionPart> parts;
        for (const view::TypingSpan& kSpan : typing.spans) {
            parts.push_back(ui::CompositionPart{.start = kSpan.start,
                                                .length = kSpan.length,
                                                .style = static_cast<ui::CompositionPart::Style>(kSpan.style)});
        }
        static_cast<void>(edit.compose(typing.text, typing.caret, parts));
        return;
    }
    case view::Typing::Kind::Key:
        break;
    }
    if (typing.key == view::TypingKey::Dismiss) {
        state.letGo();
        return;
    }
    if (typing.key != view::TypingKey::Submit) {
        static_cast<void>(edit.press(editKeyOf(typing.key), {.extend = typing.extend, .word = typing.word}));
        return;
    }
    // Enter: a line break in a field of lines, else the text given.
    if (kField.edit == 3) {
        static_cast<void>(edit.type("\n"));
        return;
    }
    state.submitted.push_back(
        view::Submitted{.press = kField.press, .text = std::string{state.tree->textOf(edit.node())}});
    ++state.statistics.submitted;
    if (kField.edit == 2) {
        // A message's field is emptied for the next.
        static_cast<void>(edit.press(ui::EditKey::SelectAll, {}));
        static_cast<void>(edit.type({}));
        return;
    }
    state.letGo();
}

std::optional<view::UiTyping::Caret> WorldUi::caret() const noexcept {
    return state_->caret;
}

std::vector<view::Submitted> WorldUi::takeSubmitted() {
    return std::exchange(state_->submitted, {});
}

const ui::DrawList& WorldUi::drawn() const noexcept {
    return state_->drawn;
}

const UiStatistics& WorldUi::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::world_ui
