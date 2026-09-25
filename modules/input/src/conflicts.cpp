#include "rawframe/input/conflicts.h"

#include <algorithm>

namespace rawframe::input {

namespace {

/// Whether some set of held modifiers activates both keyboard bindings:
/// each needs its own held, and an exact one nothing else.
bool modifiersMeet(const Binding& left, const Binding& right) noexcept {
    if (left.device != DeviceClass::Keyboard) {
        return true;
    }
    const auto kCovers = [](std::uint8_t outer, std::uint8_t inner) {
        return (outer & inner) == inner;
    };
    if (left.exactModifiers && right.exactModifiers) {
        return left.modifiers == right.modifiers;
    }
    if (left.exactModifiers) {
        return kCovers(left.modifiers, right.modifiers);
    }
    if (right.exactModifiers) {
        return kCovers(right.modifiers, left.modifiers);
    }
    return true;
}

/// The first control both bindings use, if they can activate together.
std::optional<Control> shared(const Binding& left, const Binding& right) noexcept {
    if (left.device != right.device || !modifiersMeet(left, right)) {
        return std::nullopt;
    }
    for (const Control kControl : left.controls) {
        if (kControl.valid() && std::ranges::contains(right.controls, kControl)) {
            return kControl;
        }
    }
    return std::nullopt;
}

} // namespace

std::vector<Conflict> conflicts(const ActionSet& effective, const Overrides* overrides) {
    std::vector<Conflict> found;
    for (std::size_t context = 0; context < effective.contexts.size(); ++context) {
        const std::vector<std::size_t>& members = effective.contexts[context].actions;
        for (std::size_t first = 0; first < members.size(); ++first) {
            for (std::size_t second = first + 1; second < members.size(); ++second) {
                const Action& left = effective.actions[members[first]];
                const Action& right = effective.actions[members[second]];
                for (const Binding& leftBinding : left.bindings) {
                    for (const Binding& rightBinding : right.bindings) {
                        if (const auto kControl = shared(leftBinding, rightBinding)) {
                            found.push_back(Conflict{.kind = Conflict::Kind::Collision,
                                                     .action = members[first],
                                                     .other = members[second],
                                                     .context = context,
                                                     .control = *kControl});
                        }
                    }
                }
            }
        }
    }

    // Shadowing: a context of strictly higher priority claims the control.
    // Equal priorities route by activation order, which only the running
    // game knows.
    for (std::size_t context = 0; context < effective.contexts.size(); ++context) {
        const Context& lower = effective.contexts[context];
        for (const std::size_t kAction : lower.actions) {
            for (const Binding& binding : effective.actions[kAction].bindings) {
                for (const Control kControl : binding.controls) {
                    if (!kControl.valid()) {
                        continue;
                    }
                    std::size_t claimant = Conflict::kNone;
                    for (const Context& higher : effective.contexts) {
                        if (higher.priority <= lower.priority || claimant != Conflict::kNone) {
                            continue;
                        }
                        for (const std::size_t kOther : higher.actions) {
                            const Action& other = effective.actions[kOther];
                            if (kOther == kAction || !other.consume) {
                                continue;
                            }
                            const bool kClaims = std::ranges::any_of(other.bindings, [kControl](const Binding& each) {
                                return std::ranges::contains(each.controls, kControl);
                            });
                            if (kClaims) {
                                claimant = kOther;
                                break;
                            }
                        }
                    }
                    if (claimant != Conflict::kNone) {
                        found.push_back(Conflict{.kind = Conflict::Kind::Shadowed,
                                                 .action = kAction,
                                                 .other = claimant,
                                                 .context = context,
                                                 .control = kControl});
                    }
                }
            }
        }
    }

    for (std::size_t action = 0; action < effective.actions.size(); ++action) {
        for (const Binding& binding : effective.actions[action].bindings) {
            for (const Control kControl : binding.controls) {
                if (kControl.valid() && std::ranges::contains(effective.reserved, kControl)) {
                    found.push_back(Conflict{.kind = Conflict::Kind::Reserved, .action = action, .control = kControl});
                }
            }
        }
    }

    if (overrides != nullptr) {
        for (const OverrideEntry& entry : overrides->entries) {
            if (entry.kind == OverrideEntry::Kind::Orphaned) {
                found.push_back(Conflict{.kind = Conflict::Kind::Orphaned, .identity = entry.action});
            }
        }
    }
    return found;
}

} // namespace rawframe::input
