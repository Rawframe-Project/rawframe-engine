#pragma once

// The semantic input layer's runtime (ADR-0037, SPEC-0029): control events
// from paired devices become per-player action state, routed through the
// contexts each player has active, with press and release edges accounted
// once to the simulation tick and once to the frame.

#include "rawframe/input/actions.h"
#include "rawframe/input/controls.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace rawframe::input {

/// A device, by the identity the platform keeps for it while it is
/// connected. Never a player.
struct DeviceId {
    std::uint32_t value = 0;
    friend constexpr auto operator<=>(const DeviceId&, const DeviceId&) noexcept = default;
};

/// A local player, from nought.
struct PlayerSlot {
    std::uint8_t value = 0;
    friend constexpr auto operator<=>(const PlayerSlot&, const PlayerSlot&) noexcept = default;
};

/// One report of one control: a digital control pressed (`x` one) or
/// released (nought), an axis at `x`, a stick at `x` and `y` (up and right
/// positive, each within minus one and one), or relative motion by `x` and
/// `y` since the last report.
/// A pose (ADR-0081's fourth value, D596): a place in the headset's local
/// space (meters, +Y up, its origin where the head was when its session
/// began) and a turn, a unit quaternion (x, y, z, w); whether the runtime
/// knows it (`located`), and whether it tracks it now rather than infers
/// it (`tracked`).
struct Pose {
    std::array<float, 3> position{};
    std::array<float, 4> orientation{0, 0, 0, 1};
    bool located = false;
    bool tracked = false;
};

struct ControlEvent {
    DeviceId device;
    Control control;
    float x = 0;
    float y = 0;
    /// A pose control's report.
    Pose pose;
};

/// An action's value: on or off by its thresholds, and its axes (a bool
/// action's `x` is one when on). A pose action is on while its pose is
/// located, its `x` and `y` its place's, and its pose as last located.
struct ActionState {
    bool on = false;
    float x = 0;
    float y = 0;
    Pose pose;
};

/// A press or release, in the order it happened within its tick or frame.
struct Edge {
    std::size_t action = 0;
    bool pressed = false;
    std::uint32_t ordinal = 0;
};

/// A haptic output asked for (D251): its amplitude from nought to one, its
/// frequency in hertz (nought leaves it to the device), and how long.
/// Asking again before it ends replaces it: the latest wins.
struct Haptic {
    float amplitude = 1;
    float frequency = 0;
    std::uint32_t milliseconds = 0;
};

/// What one device is asked to feel.
struct HapticCommand {
    DeviceId device;
    Haptic haptic;
};

/// SPEC-0029's named limit points for the runtime.
struct MapperLimits {
    std::size_t players = 1;
    std::size_t maximumDevices = 16;
    std::size_t maximumActiveNodes = 32;
    std::size_t maximumQueuedEventsPerPlayer = 256;
};

struct MapperStatistics {
    /// Events from devices paired to no player.
    std::uint64_t unpairedEvents = 0;
    /// Events dropped, oldest first, from a full queue; each drop releases
    /// the player's controls so nothing stays held across the gap.
    std::uint64_t droppedEvents = 0;
    /// Times a player's controls were all let go: asked for (focus lost,
    /// the platform's records not fitting) or after a drop.
    std::uint64_t releases = 0;
    /// Presses the UI took (D421), and actions turned on.
    std::uint64_t uiTaken = 0;
    std::uint64_t actionPresses = 0;
    /// Events from paired devices, by device class (its value an index):
    /// which kinds of device a player has used (D559).
    std::array<std::uint64_t, 5> events{};
};

class Mapper {
public:
    [[nodiscard]] static result::Result<std::unique_ptr<Mapper>> create(ActionSet set, const MapperLimits& limits);

    Mapper(const Mapper&) = delete;
    Mapper& operator=(const Mapper&) = delete;
    ~Mapper();

    [[nodiscard]] const ActionSet& actions() const noexcept;

    // Pairing: which player a device's controls belong to. A device is
    // paired to one player; a player may have many devices.
    [[nodiscard]] result::Status pair(DeviceId device, DeviceClass deviceClass, PlayerSlot player);
    /// Releases whatever the device held, then forgets it.
    void unpair(DeviceId device);

    // Routing: the contexts a player has active, as routing nodes.
    // Dispatch is by descending priority, then the most recently activated
    // first. Activating an active context, or enabling an enabled one, moves
    // nothing; `bringToFront` does.
    [[nodiscard]] result::Status activate(PlayerSlot player, std::size_t context);
    void deactivate(PlayerSlot player, std::size_t context);
    void setEnabled(PlayerSlot player, std::size_t context, bool enabled);
    void bringToFront(PlayerSlot player, std::size_t context);

    /// While a text field has focus, keyboard controls reach no action of a
    /// context gated by text editing; an action they held is released. A
    /// key that goes down meanwhile is the field's until it is let go
    /// (D430). Events submitted before are applied first, as the gate was:
    /// a feed's own record places it among them.
    void setTextEditing(bool editing);
    /// The UI as the topmost routing node (SPEC-0029, D421): as a mouse
    /// button or a touch control goes down, `takes` is asked whether the
    /// UI takes the press at the player's pointer on that device, in
    /// logical pixels of the window. A press it takes is the UI's alone:
    /// no action sees it held, nor its release. Never asked while the
    /// device has told no pointer. Empty (the default) takes nothing.
    using PointerTaker = std::function<bool(PlayerSlot player, float x, float y)>;
    void setPointerTaker(PointerTaker takes);
    /// Focus lost: the events submitted before it are applied, then every
    /// control of every device is released through the normal edge path.
    void releaseAll();

    /// Queues one event for its device's player.
    void submit(const ControlEvent& event);
    /// Applies what is queued: the frame's view is current after this.
    void update();
    /// Applies what is queued and commits it as the state of simulation
    /// tick `tick` (the World's tick index): values, and the edges since the
    /// last commit. Relative controls come to rest after.
    void commit(std::uint64_t tick);
    /// Ends the frame: its edges are consumed.
    void endFrame();

    // The tick domain: what the last commit holds.
    [[nodiscard]] std::uint64_t committedTick() const noexcept;
    [[nodiscard]] const ActionState& committed(PlayerSlot player, std::size_t action) const noexcept;
    [[nodiscard]] std::span<const Edge> tickEdges(PlayerSlot player) const noexcept;
    [[nodiscard]] bool pressedThisTick(PlayerSlot player, std::size_t action) const noexcept;
    [[nodiscard]] bool releasedThisTick(PlayerSlot player, std::size_t action) const noexcept;

    // The frame domain: the state now, and the edges since the last frame.
    [[nodiscard]] const ActionState& current(PlayerSlot player, std::size_t action) const noexcept;
    [[nodiscard]] std::span<const Edge> frameEdges(PlayerSlot player) const noexcept;

    /// Haptic output `haptic` of `player`, felt: a command for each device
    /// paired to the player of a class the output's bindings name, appended
    /// to `into`. None for an output or player the mapper does not have.
    void feel(PlayerSlot player, std::size_t haptic, const Haptic& felt, std::vector<HapticCommand>& into) const;

    [[nodiscard]] const MapperStatistics& statistics() const noexcept;

private:
    struct State;
    explicit Mapper(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::input
