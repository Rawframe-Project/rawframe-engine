#include "rawframe/world_ui/world_ui.h"

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
    return ui::Look{.fill = node.fill,
                    .borderColor = node.borderColor,
                    .radius = node.radius,
                    .clip = node.clip != 0,
                    .image = node.image,
                    .imageSlice = {kSlice, kSlice, kSlice, kSlice},
                    // Nought draws it as it is.
                    .imageTint = node.imageTint != 0 ? node.imageTint : 0xFFFFFFFF};
}

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
    bool seen = false;
};

using EntryKey = std::pair<world::EntityHandle, std::uint32_t>;

/// A view's root, sized to its rectangle, and the nodes its World holds.
struct ViewState {
    world::World* world = nullptr;
    std::optional<ui::Node> root;
    std::array<float, 4> placed{-1, -1, -1, -1};
    std::vector<world::ColumnQuery> queries;
    std::map<EntryKey, Entry> entries;
};

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
    ui::Node window;
    std::vector<ViewState> views;
    /// Each parent's children as last attached, by the parent's node.
    std::unordered_map<std::uint64_t, std::vector<ui::Node>> attached;
    std::uint32_t held = 0;
    ui::DrawList drawn;
    UiStatistics statistics;

    /// `entry`'s node gone, its children first made roots so they live on.
    void drop(Entry& entry) {
        if (!entry.node.has_value()) {
            return;
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
        if (!entry.node.has_value()) {
            if (!kLayout.has_value() || held >= settings.maximumNodes) {
                return false;
            }
            auto made = tree->add(keyOf(entity));
            if (!made.has_value()) {
                return false;
            }
            entry.node = *made;
            ++held;
            ++statistics.made;
        } else {
            ++statistics.changed;
        }
        if (!kLayout.has_value() || !tree->setLayout(*entry.node, *kLayout).has_value() ||
            !tree->setLook(*entry.node, lookOf(value)).has_value()) {
            drop(entry);
            return false;
        }
        return true;
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
                    // Unchanged, a node costs nothing; one refused is tried
                    // again only once it changes.
                    if (!made && std::memcmp(&entry.value, &value, sizeof(Node)) == 0) {
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
    ++state.statistics.frames;
    state.statistics.mostNodes = std::max<std::uint64_t>(state.statistics.mostNodes, state.held);
    return {};
}

const ui::DrawList& WorldUi::drawn() const noexcept {
    return state_->drawn;
}

const UiStatistics& WorldUi::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::world_ui
