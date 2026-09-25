#pragma once

// A user's rebinding (SPEC-0029's `input.overrides` document): a delta over
// an action set's default bindings, keyed by action identity, device
// class, and slot, that never changes the set it overrides. Resetting to
// the defaults is deleting entries.

#include "rawframe/document/json.h"
#include "rawframe/input/actions.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::input {

struct OverrideEntry {
    enum class Kind : std::uint8_t {
        /// The slot's binding is `binding`.
        Replace,
        /// The slot has no binding.
        Disable,
        /// Names an action the set does not have, or a slot past its limit:
        /// kept as it was read (`orphan`), never applied, written back.
        Orphaned,
    };

    std::uint64_t action = 0;
    DeviceClass device = DeviceClass::Keyboard;
    std::uint32_t slot = 0;
    Kind kind = Kind::Replace;
    Binding binding;
    document::Value orphan;
};

struct Overrides {
    /// The action set it changes, by the name the product loads it under.
    std::string target;
    /// In key order (action identity, device class, slot), each key once.
    std::vector<OverrideEntry> entries;

    /// Sets a slot's binding, which must suit the action and not be
    /// reserved. `binding.device` and `binding.slot` are the key.
    [[nodiscard]] result::Status
    rebind(const ActionSet& set, std::uint64_t action, const Binding& binding, const ActionSetLimits& limits = {});
    /// Leaves a slot without a binding.
    [[nodiscard]] result::Status disable(const ActionSet& set,
                                         std::uint64_t action,
                                         DeviceClass device,
                                         std::uint32_t slot,
                                         const ActionSetLimits& limits = {});
    /// Back to the default for one slot: its entry is deleted.
    void reset(std::uint64_t action, DeviceClass device, std::uint32_t slot);
    /// Back to every default; orphaned entries stay, as they were never
    /// this set's to delete.
    void resetAll();
};

/// Reads an `input.overrides` document of format version 1 over `set`,
/// which must be the one it names (`target`). Entries are checked like the
/// set's own bindings, and none may bind a reserved control.
[[nodiscard]] result::Result<Overrides>
readOverrides(std::string_view text, const ActionSet& set, std::string_view target, const ActionSetLimits& limits = {});

/// The document's canonical text.
[[nodiscard]] std::string writeOverrides(const Overrides& overrides);

/// The set with its overrides applied: a replaced slot keeps its default's
/// place among the action's bindings, a new one follows them, a disabled
/// one is gone.
[[nodiscard]] ActionSet applyOverrides(const ActionSet& defaults, const Overrides& overrides);

} // namespace rawframe::input
