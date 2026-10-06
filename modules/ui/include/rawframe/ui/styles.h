#pragma once

// A game's style classes as a document (SPEC-0030, D431): a `ui.styles`
// document under SPEC-0028's canonical JSON profile, as the game's actions
// and strings are, read and made into a tree's classes. Each class has a
// 16-digit identity a node names it by, a name, a look for its base and
// each state it sets one for (only the parts given), and transitions.
//
//   {
//     "kind": "ui.styles",
//     "formatVersion": 1,
//     "styles": [
//       {
//         "styleId": "6c1f0e3b9a2d4c57",
//         "name": "button",
//         "base": {"fill": "#2a3140ff", "radius": 6},
//         "hovered": {"fill": "#3a4458ff"},
//         "pressed": {"fill": "#1c222cff"},
//         "focused": {"outerShadow": {"color": "#ffd24aff", "spread": 2}},
//         "transitions": [{"parts": ["fill"], "seconds": 0.12, "easing": "ease_out"}]
//       }
//     ]
//   }
//
// A look's parts are `fill`, `borderColor`, `radius`, `clip`, `imageTint`,
// `outerShadow` and `innerShadow` (`color`, `x`, `y`, `blur`, `spread`),
// and `gradient` (`kind` linear or radial, `angle`, `from`, `to`); colors
// are `#rrggbbaa`, sRGB with straight alpha. A transition is `timed` (the
// default: `seconds`, `delaySeconds`, `easing` of linear, ease, ease_in,
// ease_out, ease_in_out, or cubic_bezier with `bezier`) or `spring`
// (`frequency`, `dampingRatio`), for every state of its class.

#include "rawframe/result/result.h"
#include "rawframe/ui/tree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::ui {

/// What one variant of a class sets: its `parts` of `look`.
struct StyleLook {
    Look look;
    LookParts parts = 0;
};

/// One class as its document declares it, its looks by Variant.
struct StyleClass {
    std::uint64_t id = 0;
    std::string name;
    std::array<StyleLook, 5> looks{};
    std::vector<std::pair<LookParts, Transition>> transitions;
};

struct StyleSheet {
    std::vector<StyleClass> styles;

    [[nodiscard]] std::optional<std::size_t> styleWithId(std::uint64_t id) const noexcept;
};

/// The named limit points of a styles document; generation 1's.
struct StyleLimits {
    std::size_t maximumStyles = 256;
    std::size_t maximumTransitions = 16;
    std::size_t maximumNameLength = 64;
};

/// Reads a `ui.styles` document of format version 1: canonical bytes, the
/// version, each record's fields in order, identities used once, names,
/// colors, ranges, limits. Refusals are `rawframe.document` errors naming
/// the field by its path.
[[nodiscard]] result::Result<StyleSheet> readStyles(std::string_view text, const StyleLimits& limits = {});

/// Every class of `sheet` made in `tree`, in the sheet's order.
[[nodiscard]] result::Result<std::vector<Style>> addStyles(Tree& tree, const StyleSheet& sheet);

} // namespace rawframe::ui
