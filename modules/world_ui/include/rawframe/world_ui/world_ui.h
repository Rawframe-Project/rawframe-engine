#pragma once

// The UI as World data (ADR-0034, D376): every `rawframe.ui.Node` component
// of the World a local player's client mirrors is a node of one UI tree,
// keyed by its entity, nested by the game's `ui` lines, and laid out in the
// player's view. What it draws is SPEC-0032's draw-command list, for the
// device to draw over the scene and the canvas. Client only: a dedicated
// server carries the components as values and links none of this.

#include "rawframe/result/result.h"
#include "rawframe/schema/component.h"
#include "rawframe/ui/tree.h"
#include "rawframe/world/entity.h"
#include "rawframe/world/world.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::world_ui {

/// `rawframe.ui.Node` as Kest lays it out (rawframe/ui.kest).
struct Node {
    float widthScale = 0;
    float widthOffset = 0;
    float heightScale = 0;
    float heightOffset = 0;
    std::uint32_t direction = 0;
    std::uint32_t justify = 0;
    std::uint32_t alignItems = 0;
    std::uint32_t alignSelf = 0;
    float gap = 0;
    float grow = 0;
    float shrink = 0;
    float padding = 0;
    float margin = 0;
    float border = 0;
    std::uint32_t absolute = 0;
    float xScale = 0;
    float xOffset = 0;
    float yScale = 0;
    float yOffset = 0;
    float anchorX = 0;
    float anchorY = 0;
    std::uint32_t fill = 0;
    std::uint32_t borderColor = 0;
    float radius = 0;
    std::uint32_t clip = 0;
    std::int32_t order = 0;
    std::uint64_t image = 0;
    std::uint32_t imageTint = 0;
    float imageSlice = 0;
    std::uint32_t shadowColor = 0;
    float shadowX = 0;
    float shadowY = 0;
    float shadowBlur = 0;
};

/// The game's node components, in declaration order, and the one each is
/// inside by its `ui` line, an index of them; none for a root of its view.
struct UiSettings {
    std::vector<schema::ComponentTypeId> nodes;
    std::vector<std::optional<std::size_t>> parents;
    /// The most nodes held at once, every view's together; a node past them
    /// is left out and counted.
    std::uint32_t maximumNodes = 4096;
};

/// A local player's view as the UI lays it out: the World its client
/// mirrors (none while it has none) and its own player in it, and the
/// view's rectangle in the window, logical pixels from its top left.
struct UiView {
    world::World* world = nullptr;
    world::EntityHandle player;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

struct UiStatistics {
    /// Frames laid out and drawn.
    std::uint64_t frames = 0;
    /// Nodes made, and changed after: each costs a layout and a paint.
    std::uint64_t made = 0;
    std::uint64_t changed = 0;
    /// Nodes left out in a frame, summed: past the most held, or inside a
    /// parent neither their entity nor the player has, or with a value the
    /// tree refuses (a negative size or radius, an anchor past one).
    std::uint64_t leftOut = 0;
    /// The most nodes a frame held.
    std::uint64_t mostNodes = 0;
};

class WorldUi {
public:
    /// Refused for a parent index out of range.
    [[nodiscard]] static result::Result<std::unique_ptr<WorldUi>> create(UiSettings settings);

    WorldUi(const WorldUi&) = delete;
    WorldUi& operator=(const WorldUi&) = delete;
    ~WorldUi();

    /// One frame: each view's World read, its nodes made, changed, or
    /// removed as its components are, laid out in its rectangle of a window
    /// `width` by `height` logical pixels, and drawn, `scale` device pixels
    /// each. A view whose World is another than the last starts afresh.
    /// The roots of a view are laid out in a column from its top left, in
    /// `order` and then by entity, each its own size, an absolute one where
    /// it says; children likewise in their parent.
    [[nodiscard]] result::Status update(std::span<const UiView> views, float width, float height, float scale);

    /// What the last update drew.
    [[nodiscard]] const ui::DrawList& drawn() const noexcept;
    [[nodiscard]] const UiStatistics& statistics() const noexcept;

    struct State;

private:
    explicit WorldUi(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::world_ui
