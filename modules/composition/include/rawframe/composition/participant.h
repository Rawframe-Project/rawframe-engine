#pragma once

#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rawframe::composition {

class ParticipantContext;

/// The Host schedule's phases, in order (SPEC-0005 Host schedule). A
/// composition may leave a phase without providers; a dedicated server has no
/// presentation phases at all. Nothing may reorder them.
enum class HostPhase : std::uint8_t {
    PlatformPoll,
    Ingress,
    AdvanceClocks,
    RunWorlds,
    PresentationExtract,
    Present,
    Egress,
    Maintenance,
    FrameEnd,
};

inline constexpr std::size_t kHostPhaseCount = 9;

[[nodiscard]] constexpr std::uint16_t hostPhaseBit(HostPhase phase) noexcept {
    return static_cast<std::uint16_t>(1U << static_cast<unsigned>(phase));
}

/// One Host iteration, as each phase sees it.
struct HostFrame {
    std::uint64_t iteration = 0;
    /// Sampled once per iteration, in `advance_clocks`.
    execution::MonotonicInstant now;
};

namespace detail {

// One distinct address per type: a type tag without RTTI.
template <typename T> inline constexpr char kTypeTag = 0;

} // namespace detail

/// A named interface one participant provides and others require. Declared
/// once, as a constant next to the interface, so provider and consumer agree on
/// both the name and the type.
template <typename Interface> struct Capability {
    std::string_view name;
};

/// A provided interface as the plan carries it: the object and a tag naming its
/// type, checked when a consumer asks for it.
struct CapabilityObject {
    const void* typeTag = nullptr;
    void* object = nullptr;
};

template <typename Interface> [[nodiscard]] CapabilityObject provideAs(Interface& object) noexcept {
    return CapabilityObject{&detail::kTypeTag<Interface>, &object};
}

/// A runtime participant: a service or system that composition constructs,
/// starts, quiesces, stops, and destroys (SPEC-0005 service lifecycle).
/// Construction happens in its factory and must not start work; running
/// begins only when `start` succeeds.
class Participant {
public:
    Participant() = default;
    Participant(const Participant&) = delete;
    Participant& operator=(const Participant&) = delete;
    virtual ~Participant() = default;

    /// Begins owned work and publishes readiness. A failure here rolls the
    /// whole composition back.
    [[nodiscard]] virtual result::Status start(ParticipantContext& context) noexcept;

    /// Closes admission and cancels owned work. Called before `stop`, in
    /// reverse start order, and also when a later participant failed to start.
    virtual void quiesce() noexcept;

    /// Drains and joins owned work within the declared stop budget, then
    /// releases what it holds. Called only after `quiesce`.
    virtual void stop() noexcept;

    /// Work for one Host phase this participant declared in `hostPhases`.
    /// Called on the Host's thread, in plan order, once per iteration.
    virtual void runHostPhase(HostPhase phase, const HostFrame& frame) noexcept;

    /// The object behind one of the capabilities this participant declared it
    /// provides, via `provideAs`, or an empty object for any other name.
    [[nodiscard]] virtual CapabilityObject provide(std::string_view capability) noexcept;
};

} // namespace rawframe::composition
