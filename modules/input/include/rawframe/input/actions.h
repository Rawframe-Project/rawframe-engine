#pragma once

// An action set (SPEC-0029's `input.actions` document): the typed actions a
// game reads, their default bindings, and the contexts that route them.
// Parsed and checked, never executed.

#include "rawframe/input/controls.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::input {

/// An action's one value type (ADR-0037).
enum class ValueType : std::uint8_t {
    Bool,
    Axis1D,
    Axis2D
};

/// How a binding's controls make a value: one control, two digital controls
/// making one axis (`negative`, `positive`), or four making two (`up`,
/// `down`, `left`, `right`).
enum class Composite : std::uint8_t {
    None,
    Pair,
    Quad
};

/// Whether a quad's diagonal is clamped to the unit circle
/// (`digital_normalized`, the default) or left at its corner (`digital`).
enum class CompositeMode : std::uint8_t {
    DigitalNormalized,
    Digital
};

struct Binding {
    std::uint32_t slot = 0;
    DeviceClass device = DeviceClass::Keyboard;
    Composite composite = Composite::None;
    /// The one control, or a pair's negative and positive, or a quad's up,
    /// down, left, and right.
    std::array<Control, 4> controls{};
    CompositeMode mode = CompositeMode::DigitalNormalized;
    /// Modifier bits a keyboard binding needs held; with `exactModifiers`,
    /// no others may be.
    std::uint8_t modifiers = 0;
    bool exactModifiers = false;
    /// An axis control's deadzone: magnitudes up to `deadzoneLower` read as
    /// nought, from `deadzoneUpper` as one, and between inverse-lerped.
    float deadzoneLower = 0.2F;
    float deadzoneUpper = 1.0F;
    bool invertX = false;
    bool invertY = false;
    float scale = 1.0F;
};

struct Action {
    /// Durable identity: what overrides and derivations key on.
    std::uint64_t id = 0;
    std::string name;
    ValueType type = ValueType::Bool;
    std::string displayName;
    std::string group;
    /// Digital activation from magnitude: on at `pressThreshold`, off below
    /// `releaseThreshold`.
    float pressThreshold = 0.5F;
    float releaseThreshold = 0.5F;
    /// Whether the contexts holding it claim its controls from lower ones.
    bool consume = true;
    std::vector<Binding> bindings;
};

struct Context {
    std::uint64_t id = 0;
    std::string name;
    std::int64_t priority = 0;
    /// Indices into the set's actions.
    std::vector<std::size_t> actions;
    bool textEditGated = true;
};

struct ActionSet {
    std::vector<Action> actions;
    std::vector<Context> contexts;
    /// Controls the product keeps for itself; no override may bind them.
    std::vector<Control> reserved;

    /// The index of the action of that name, or of that identity.
    [[nodiscard]] std::optional<std::size_t> actionNamed(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::size_t> actionWithId(std::uint64_t id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> contextNamed(std::string_view name) const noexcept;
};

/// SPEC-0029's named limit points for an action set. Values are the
/// product's to set; these are generation 1's.
struct ActionSetLimits {
    std::size_t maximumActions = 256;
    std::size_t maximumContexts = 64;
    std::size_t maximumSlotsPerDeviceClass = 4;
    std::size_t maximumNameLength = 64;
    std::size_t maximumDisplayLength = 128;
};

/// Reads an `input.actions` document of format version 1, in SPEC-0029's
/// order: canonical bytes, the version, each record's fields, identities
/// and names, controls and composites, ranges, references, limits. Refusals
/// are `rawframe.document` errors naming the field by its path.
[[nodiscard]] result::Result<ActionSet> readActionSet(std::string_view text, const ActionSetLimits& limits = {});

/// A 16-digit lowercase hexadecimal identity, as actions and contexts have.
[[nodiscard]] std::optional<std::uint64_t> parseIdentity(std::string_view text) noexcept;

} // namespace rawframe::input
