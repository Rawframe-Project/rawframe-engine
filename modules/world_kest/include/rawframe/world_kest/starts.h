#pragma once

// What a presentation component starts as when presentation attaches it to
// an entity, where a document the presentation does not read says so
// (D619): a client's UI document starts its node components, and the
// game's present systems write only what changes. The client's UI
// provides it (`rawframe.world_ui`); without it, or for a component it
// does not start, a component starts at nought.

#include "rawframe/composition/participant.h"
#include "rawframe/schema/component.h"

#include <cstddef>
#include <span>

namespace rawframe::world_kest {

class PresentationStarts {
public:
    PresentationStarts() = default;
    PresentationStarts(const PresentationStarts&) = delete;
    PresentationStarts& operator=(const PresentationStarts&) = delete;
    virtual ~PresentationStarts() = default;

    /// The bytes `component` starts as, its value's size; empty for nought.
    [[nodiscard]] virtual std::span<const std::byte> startOf(schema::ComponentTypeId component) const noexcept = 0;
};

inline constexpr composition::Capability<PresentationStarts> kPresentationStarts{
    "rawframe.world_kest.presentation_starts"};

} // namespace rawframe::world_kest
